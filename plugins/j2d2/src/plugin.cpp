// J2D2: an MPE droid synth. Every note speaks in droid phrases, hums a layered drone, or both,
// set by the DROID <-> DRONE morph. The sound is in engine.h; this file is the CLAP side: params,
// state, notes and MPE expression, and what the panel reads.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/sample_fifo.h>
#include <jclap/dsp/denormals.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace j2d2;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO,
                                 nullptr};

// What a MIDI channel last sent. MPE controllers send a note's bend, pressure and slide on its
// channel just before the note-on, so the note picks them up from here.
struct MidiChannel {
   double bend = 0.0; // -1..+1
   double pressure = 0.0, timbre = 0.5;
   double bendRange = -1.0; // semitones from RPN 0, or -1 for the default
   int rpnMsb = 127, rpnLsb = 127;
};

// State: 4-byte magic, uint32 version, uint32 param count, one double per param in kParams order,
// then the preset name (uint32 length and its bytes). Params are only ever appended, so a state
// with fewer values loads those and leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', '2', 'D', '2'};
constexpr uint32_t kStateVersion = 1;

// The panel's copy of one voice, written by the audio thread once a block.
struct SharedVoice {
   std::atomic<bool> active{false};
   std::atomic<int> note{-1}, event{kEvNone};
   std::atomic<float> energy{0}, feeling{0}, pressure{0}, droid{0}, drone{0}, pan{0}, eventPitch{0};
};

class J2D2 final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit J2D2(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      engine_.prepare(sampleRate);
      resetMidi();
      return true;
   }

   void deactivate() override {
      for (SharedVoice &v : shown_)
         v.active.store(false);
   }

   void reset() override {
      engine_.reset();
      resetMidi();
   }

   uint32_t audioPortsCount(bool isInput) const override { return isInput ? 0 : 1; }
   bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const override {
      if (!Plugin::audioPortsInfo(index, isInput, info))
         return false;
      info->in_place_pair = CLAP_INVALID_ID;
      return true;
   }
   uint32_t notePortsCount(bool isInput) const override { return isInput ? 1 : 0; }
   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      // CLAP note events carry per-note expression (MPE) directly.
      info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
      info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
      return true;
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(*this);
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<J2D2, &J2D2::gui_>(id);
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double value) override {
      if (id >= kParamCount || !std::isfinite(value))
         return;
      value = std::clamp(value, kParams[id].min, kParams[id].max);
      values_[id].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   void outputPeaks(float &left, float &right) override {
      left = peakL_.exchange(0.0f);
      right = peakR_.exchange(0.0f);
   }
   void readScope(float *out, size_t count) override { scope_.read(out, count); }
   Voice voice(int index) const override {
      Voice v;
      if (index < 0 || index >= kMaxVoices)
         return v;
      const SharedVoice &s = shown_[index];
      v.active = s.active.load();
      v.note = s.note.load();
      v.energy = s.energy.load();
      v.feeling = s.feeling.load();
      v.pressure = s.pressure.load();
      v.droid = s.droid.load();
      v.drone = s.drone.load();
      v.pan = s.pan.load();
      v.event = s.event.load();
      v.eventPitch = s.eventPitch.load();
      return v;
   }
   float lfo(int index) const override { return lfoShown_[index & 1].load(); }
   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      jclap::core::fillParamInfo(kParams[index], info);
      return true;
   }

   bool paramsValue(clap_id id, double *value) const override {
      if (id >= kParamCount)
         return false;
      *value = values_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, value, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      if (id >= kParamCount || !parseParam(id, text, *value))
         return false;
      *value = std::clamp(*value, kParams[id].min, kParams[id].max);
      return true;
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleEvent(in->get(in, i));
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t header[2] = {kStateVersion, kParamCount};
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, header, sizeof(header)))
         return false;
      for (size_t i = 0; i < kParamCount; ++i) {
         const double v = values_[i].load();
         if (!jclap::core::writeAll(stream, &v, sizeof(v)))
            return false;
      }
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t header[2] = {};
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, header, sizeof(header)) || header[0] < 1 || header[0] > kStateVersion ||
          header[1] > 4096)
         return false;
      // Params the state doesn't have, or has as NaN or infinity, go back to their defaults.
      double v[kParamCount];
      for (size_t i = 0; i < kParamCount; ++i)
         v[i] = kParams[i].defaultValue;
      for (uint32_t i = 0; i < header[1]; ++i) {
         double x;
         if (!jclap::core::readAll(stream, &x, sizeof(x)))
            return false;
         if (i < kParamCount && std::isfinite(x))
            v[i] = x;
      }
      uint32_t nameLength = 0;
      char name[256];
      if (jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) && nameLength < sizeof(name) &&
          jclap::core::readAll(stream, name, nameLength))
         presetName_.assign(name, nameLength);
      else
         presetName_.clear();
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], kParams[i].min, kParams[i].max));
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      jclap::dsp::ScopedNoDenormals noDenormals;
      edits_.drain(proc->out_events);
      if (proc->audio_outputs_count < 1 || !proc->audio_outputs[0].data32 || proc->audio_outputs[0].channel_count < 1)
         return CLAP_PROCESS_CONTINUE;
      float *left = proc->audio_outputs[0].data32[0];
      float *right = proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : nullptr;
      const uint32_t frames = proc->frames_count;
      std::fill(left, left + frames, 0.0f);
      if (right)
         std::fill(right, right + frames, 0.0f);
      else
         right = left; // mono out: both halves add into the one channel

      // The host's tempo, for a synced delay.
      if (const clap_event_transport_t *t = proc->transport)
         if (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
            engine_.setTempo(t->tempo);

      // Events are sample accurate: render up to each one, then apply it.
      const clap_input_events_t *in = proc->in_events;
      const uint32_t eventCount = in->size(in);
      uint32_t next = 0;
      float peakL = 0.0f, peakR = 0.0f;
      for (uint32_t start = 0; start < frames;) {
         while (next < eventCount && in->get(in, next)->time <= start)
            handleEvent(in->get(in, next++));
         const uint32_t end = next < eventCount ? std::min(frames, std::max(start + 1, in->get(in, next)->time)) : frames;
         refreshSettings();
         if (right == left) {
            float scratch[256];
            for (uint32_t i = start; i < end; i += 256) {
               const int n = static_cast<int>(std::min<uint32_t>(256, end - i));
               std::fill(scratch, scratch + n, 0.0f);
               engine_.process(left + i, scratch, n, settings_);
               for (int k = 0; k < n; ++k)
                  left[i + k] = 0.5f * (left[i + k] + scratch[k]);
            }
         } else {
            engine_.process(left + start, right + start, static_cast<int>(end - start), settings_);
         }
         start = end;
      }
      while (next < eventCount)
         handleEvent(in->get(in, next++));
      for (uint32_t i = 0; i < frames; ++i) {
         peakL = std::max(peakL, std::fabs(left[i]));
         peakR = std::max(peakR, std::fabs(right[i]));
         mono_[i & 255] = 0.5f * (left[i] + right[i]);
         if ((i & 255) == 255 || i + 1 == frames)
            scope_.write(mono_, (i & 255) + 1);
      }
      publish();
      float seen = peakL_.load(std::memory_order_relaxed);
      while (peakL > seen && !peakL_.compare_exchange_weak(seen, peakL)) {
      }
      seen = peakR_.load(std::memory_order_relaxed);
      while (peakR > seen && !peakR_.compare_exchange_weak(seen, peakR)) {
      }
      return engine_.activeVoices() > 0 ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
   }

private:
   void refreshSettings() {
      for (size_t i = 0; i < kParamCount; ++i)
         settings_.v[i] = value(static_cast<clap_id>(i));
   }

   void handleEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      switch (h->type) {
      case CLAP_EVENT_PARAM_VALUE: {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->value))
            values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
         break;
      }
      case CLAP_EVENT_PARAM_MOD: {
         // A host modulator's offset (Bitwig's modulators send these). It adds to the param
         // without changing it.
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->amount))
            mods_[ev->param_id].store(ev->amount);
         break;
      }
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         if (h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0) {
            if (ev->key >= 0 && ev->key < 128) {
               refreshSettings();
               engine_.noteOn(ev->key, -1, ev->note_id, ev->velocity, settings_);
            }
         } else if (ev->key == -1 && ev->note_id == -1)
            engine_.releaseAll();
         else
            engine_.noteOff(ev->key, -1, ev->note_id);
         break;
      }
      case CLAP_EVENT_NOTE_CHOKE: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         engine_.choke(ev->key, -1, ev->note_id);
         break;
      }
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         if (!std::isfinite(ev->value))
            break;
         if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
            engine_.bend(ev->key, -1, ev->note_id, std::clamp(ev->value, -120.0, 120.0));
         else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
            engine_.pressure(ev->key, -1, ev->note_id, ev->value);
         else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
            engine_.slide(ev->key, -1, ev->note_id, ev->value);
         break;
      }
      case CLAP_EVENT_MIDI: {
         // Plain MIDI, or MPE where each note has its own channel. Bend is +/-2 semitones, or
         // what RPN 0 sets; once an MPE configuration message (RPN 6 on channel 1) turns on a
         // zone, its note channels (2..) bend +/-48 and channel 1 bends every note. Channel
         // pressure is pressure and CC 74 is slide, on the note's own channel.
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         MidiChannel &c = midi_[channel];
         if (status == 0x90 && ev->data[2] > 0) {
            refreshSettings();
            const int key = ev->data[1];
            engine_.noteOn(key, channel, -1, ev->data[2] / 127.0, settings_);
            engine_.pressure(key, channel, -1, c.pressure);
            engine_.slide(key, channel, -1, c.timbre);
            engine_.bend(key, channel, -1, midiBend(channel));
            channelKey_[channel] = key;
         } else if (status == 0x80 || status == 0x90)
            engine_.noteOff(ev->data[1], channel, -1);
         else if (status == 0xE0) {
            c.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0;
            applyBend(channel);
         } else if (status == 0xD0) {
            c.pressure = ev->data[1] / 127.0;
            if (channelKey_[channel] >= 0)
               engine_.pressure(channelKey_[channel], channel, -1, c.pressure);
         } else if (status == 0xA0) {
            engine_.pressure(ev->data[1], channel, -1, ev->data[2] / 127.0); // poly aftertouch
         } else if (status == 0xB0 && ev->data[1] == 74) {
            c.timbre = ev->data[2] / 127.0;
            if (channelKey_[channel] >= 0)
               engine_.slide(channelKey_[channel], channel, -1, c.timbre);
         } else if (status == 0xB0 && ev->data[1] == 101)
            c.rpnMsb = ev->data[2];
         else if (status == 0xB0 && ev->data[1] == 100)
            c.rpnLsb = ev->data[2];
         else if (status == 0xB0 && ev->data[1] == 6)
            dataEntry(channel, ev->data[2]);
         else if (status == 0xB0 && ev->data[1] == 123)
            engine_.releaseAll(); // All Notes Off
         else if (status == 0xB0 && ev->data[1] == 120)
            engine_.choke(-1, -1, -1); // All Sound Off
         break;
      }
      default:
         break;
      }
   }

   void resetMidi() {
      for (MidiChannel &c : midi_)
         c = MidiChannel{};
      for (int &k : channelKey_)
         k = -1;
      mpeMembers_ = 0;
   }

   bool isMember(int channel) const { return channel >= 1 && channel <= mpeMembers_; }
   double bendRange(int channel) const {
      const double r = midi_[channel].bendRange;
      return r >= 0.0 ? r : isMember(channel) ? 48.0 : 2.0;
   }
   // A MIDI note's bend in semitones: its own channel's, plus channel 1's when it's in an MPE zone.
   double midiBend(int ch) const {
      double bend = midi_[ch].bend * bendRange(ch);
      if (isMember(ch))
         bend += midi_[0].bend * bendRange(0);
      return bend;
   }
   void applyBend(int channel) {
      if (channel == 0 && mpeMembers_ > 0) {
         for (int ch = 1; ch <= mpeMembers_; ++ch)
            if (channelKey_[ch] >= 0)
               engine_.bend(channelKey_[ch], ch, -1, midiBend(ch));
      }
      if (channelKey_[channel] >= 0)
         engine_.bend(channelKey_[channel], channel, -1, midiBend(channel));
   }

   // CC 6 for the RPN selected with CC 101/100: 0 is the bend range in semitones, and 6 on channel 1
   // is the MPE configuration message, whose value is how many note channels the zone has.
   void dataEntry(int channel, int value) {
      const MidiChannel &c = midi_[channel];
      if (c.rpnMsb != 0)
         return;
      if (c.rpnLsb == 0)
         midi_[channel].bendRange = value;
      else if (c.rpnLsb == 6 && channel == 0) {
         mpeMembers_ = std::min(value, 15);
         for (MidiChannel &m : midi_)
            m.bendRange = -1.0;
      }
   }

   // Copies each voice where the panel can read it.
   void publish() {
      for (int i = 0; i < kMaxVoices; ++i) {
         const VoiceView v = engine_.view(i);
         SharedVoice &s = shown_[i];
         s.note.store(v.note);
         s.energy.store(static_cast<float>(v.energy));
         s.feeling.store(static_cast<float>(v.feeling));
         s.pressure.store(static_cast<float>(v.pressure));
         s.droid.store(static_cast<float>(v.droid));
         s.drone.store(static_cast<float>(v.drone));
         s.pan.store(static_cast<float>(v.pan));
         s.event.store(v.event);
         s.eventPitch.store(static_cast<float>(v.eventPitch));
         s.active.store(v.active);
      }
      lfoShown_[0].store(static_cast<float>(engine_.lfo(0)));
      lfoShown_[1].store(static_cast<float>(engine_.lfo(1)));
   }

   // A param as the audio hears it: its value plus host modulation, clamped to its range.
   double value(clap_id id) const { return std::clamp(values_[id].load() + mods_[id].load(), kParams[id].min, kParams[id].max); }

   std::atomic<double> values_[kParamCount];
   std::atomic<double> mods_[kParamCount];
   std::atomic<float> peakL_{0.0f}, peakR_{0.0f};
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;

   double sampleRate_ = 48000.0;
   Engine engine_;
   Settings settings_;
   MidiChannel midi_[16];
   int channelKey_[16] = {};
   int mpeMembers_ = 0;
   float mono_[256] = {};
   jclap::core::SampleFifo<4096> scope_;
   SharedVoice shown_[kMaxVoices];
   std::atomic<float> lfoShown_[2] = {};
   std::string presetName_ = "Init";

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t J2D2::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.j2d2",
   "J2D2",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "An MPE droid synth: droid chatter, layered drones, or any blend",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(J2D2)
