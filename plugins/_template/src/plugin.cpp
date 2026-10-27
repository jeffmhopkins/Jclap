// Plugin Template: a stereo gain/pan/width utility plus a one-voice test tone, with a panel that uses
// every jclap::ui widget. It exists to be copied; see README.md. Everything plugin-specific is in this file and editor.cpp.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/sample_fifo.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/dsp/smoother.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "editor.h"
#include "params.h"

namespace {

using namespace plugin_template;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// A note from the panel to the audio thread.
struct PanelNote {
   enum class Kind : uint8_t { On, Expression, Off } kind;
   int8_t note;
   float velocity, bend, pressure, timbre;
};

// One voice: an oscillator through a linear ADSR, last note wins.
struct ToneVoice {
   enum class Stage { Off, Attack, Decay, Sustain, Release } stage = Stage::Off;
   int note = -1;
   // The MIDI channel a MIDI note came in on, whose bend, pressure and CC 74 it follows; -1 for CLAP
   // and panel notes, which get per-note expressions instead.
   int channel = -1;
   double phase = 0.0, level = 0.0, velocity = 0.0;
   // Per-note expression: bend (semitones) moves the pitch; pressure and timbre are only shown.
   double bend = 0.0, pressure = 0.0, timbre = 0.5;
};

// What a MIDI channel last sent. MPE controllers send a note's bend, pressure and timbre on its
// channel just before the note-on, so the note picks them up from here.
struct MidiChannel {
   double bend = 0.0; // -1..+1
   double pressure = 0.0, timbre = 0.5;
   double bendRange = -1.0; // semitones from RPN 0, or -1 for the default
   int rpnMsb = 127, rpnLsb = 127;
};

// State layout: 4-byte magic, uint32 version, uint32 param count, then one double per param in
// kParams order. Params are only ever appended, so a state with fewer values loads those and leaves
// the rest at their defaults, and one with more (from a newer build) ignores the extras. Version 2
// (0.6.0 and earlier) had no count and always 12 values.
constexpr char kStateMagic[4] = {'J', 'T', 'M', 'P'};
constexpr uint32_t kStateVersion = 3;
constexpr uint32_t kVersion2ParamCount = 12;

class PluginTemplate final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit PluginTemplate(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      voice_ = ToneVoice{};
      resetMidi();
      // Panel notes played while inactive are dropped, not replayed now. The audio thread isn't
      // running, so popping here is safe.
      PanelNote stale;
      while (panelNotes_.pop(stale)) {
      }
      active_.store(true);
      // Param changes glide over about 20 ms instead of jumping once per block. They start where the
      // params are now, so activating doesn't fade in.
      for (jclap::dsp::Smoother *s : {&gainL_, &gainR_, &width_}) {
         s->setSampleRate(sampleRate);
         s->setTime(0.02);
      }
      float g[2];
      mixerGains(g);
      gainL_.reset(g[0]);
      gainR_.reset(g[1]);
      width_.reset(value(kWidth));
      return true;
   }

   // The panel's lit keys and voice view go dark with the sound.
   void deactivate() override {
      active_.store(false);
      voice_ = ToneVoice{};
      clearHeld();
      publishVoice();
   }

   void reset() override {
      voice_ = ToneVoice{};
      resetMidi();
      clearHeld();
      publishVoice();
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
      gui_.init(host());
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
      return jclap::ui::ClapGui::extensionFor<PluginTemplate, &PluginTemplate::gui_>(id);
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
   float outputPeak() override { return peak_.exchange(0.0f); }
   void readScope(float *out, size_t count) override { scope_.read(out, count); }
   // While inactive nothing plays the queue, so the panel's notes are ignored rather than queued.
   void noteOn(int note, float velocity) override {
      if (active_.load())
         panelNotes_.push({PanelNote::Kind::On, static_cast<int8_t>(note), std::max(0.01f, velocity), 0, 0, 0.5f});
   }
   void noteExpression(int note, float bend, float pressure, float timbre) override {
      if (active_.load())
         panelNotes_.push({PanelNote::Kind::Expression, static_cast<int8_t>(note), 0, bend, pressure, timbre});
   }
   void noteOff(int note) override {
      if (active_.load())
         panelNotes_.push({PanelNote::Kind::Off, static_cast<int8_t>(note), 0, 0, 0, 0});
   }
   bool voice(Voice &out) const override {
      const int note = shownNote_.load();
      if (note < 0)
         return false;
      out = {note, shownBend_.load(), shownPressure_.load(), shownTimbre_.load()};
      return true;
   }
   bool noteHeld(int note) const override {
      return note >= 0 && note < 128 && (held_[note / 64].load() >> (note % 64) & 1u);
   }
   uint32_t noteCount() const override { return noteCount_.load(); }

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
      handleEvents(in);
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
      return true;
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t version = 0, count = 0;
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4))
         return false;
      if (version == 2)
         count = kVersion2ParamCount;
      else if (version != kStateVersion || !jclap::core::readAll(stream, &count, 4))
         return false;
      // Params the state doesn't have, or has as NaN or infinity, go back to their defaults.
      double v[kParamCount];
      for (size_t i = 0; i < kParamCount; ++i)
         v[i] = kParams[i].defaultValue;
      for (uint32_t i = 0; i < count; ++i) {
         double x;
         if (!jclap::core::readAll(stream, &x, sizeof(x)))
            return false;
         if (i < kParamCount && std::isfinite(x))
            v[i] = x;
      }
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], kParams[i].min, kParams[i].max));
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      edits_.drain(proc->out_events);
      // For brevity every event applies at the start of the block. A plugin that needs
      // sample-accurate timing would split the block at each event's header.time.
      handleEvents(proc->in_events);
      PanelNote p;
      while (panelNotes_.pop(p)) {
         if (p.kind == PanelNote::Kind::On)
            startNote(p.note, p.velocity);
         else if (p.kind == PanelNote::Kind::Off)
            stopNote(p.note);
         else if (p.note == voice_.note) {
            voice_.bend = p.bend;
            voice_.pressure = p.pressure;
            voice_.timbre = p.timbre;
         }
      }
      if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
         return CLAP_PROCESS_CONTINUE;

      // Routing, then mid/side width, then constant-power pan on top of the gain; mute wins. The
      // test tone is added to the input before all of that. Gain and width glide to their new
      // values, so automation doesn't zipper and mute doesn't click.
      float g[2];
      mixerGains(g);
      gainL_.setTarget(g[0]);
      gainR_.setTarget(g[1]);
      width_.setTarget(value(kWidth));

      const clap_audio_buffer_t &in = proc->audio_inputs[0];
      const clap_audio_buffer_t &out = proc->audio_outputs[0];
      if (!in.data32 || !out.data32 || in.channel_count < 2 || out.channel_count < 2) {
         jclap::core::passAudioThrough(proc);
         return CLAP_PROCESS_CONTINUE;
      }
      const int routing = static_cast<int>(value(kRouting) + 0.5);
      float peak = 0.0f;
      for (uint32_t i = 0; i < proc->frames_count; ++i) {
         const float tone = nextToneSample();
         float l = in.data32[0][i] + tone, r = in.data32[1][i] + tone;
         if (routing == 1)
            std::swap(l, r);
         else if (routing == 2)
            r = l;
         else if (routing == 3)
            l = r;
         const float width = static_cast<float>(width_.process());
         const float gl = static_cast<float>(gainL_.process()), gr = static_cast<float>(gainR_.process());
         const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
         out.data32[0][i] = (mid + side) * gl;
         out.data32[1][i] = (mid - side) * gr;
         peak = std::max(peak, std::max(std::fabs(out.data32[0][i]), std::fabs(out.data32[1][i])));
      }
      scope_.write(out.data32[0], proc->frames_count);
      publishVoice();
      // The panel takes the largest peak since it last looked.
      float seen = peak_.load(std::memory_order_relaxed);
      while (peak > seen && !peak_.compare_exchange_weak(seen, peak)) {
      }
      return CLAP_PROCESS_CONTINUE;
   }

private:
   void handleEvents(const clap_input_events_t *in) {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         switch (h->type) {
         case CLAP_EVENT_PARAM_VALUE: {
            auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->value))
               values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
            break;
         }
         case CLAP_EVENT_PARAM_MOD: {
            // A host modulator's offset (Bitwig's modulators send these). It adds to the param
            // without changing it; the knobs show it as their light ring.
            auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->amount))
               mods_[ev->param_id].store(ev->amount);
            break;
         }
         case CLAP_EVENT_NOTE_ON:
         case CLAP_EVENT_NOTE_OFF: {
            auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
            if (h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0)
               startNote(ev->key, static_cast<float>(ev->velocity));
            else if (ev->key == -1)
               releaseAll();
            else
               stopNote(ev->key);
            break;
         }
         case CLAP_EVENT_NOTE_CHOKE: {
            // Stop at once, no release.
            auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
            if (ev->key == -1 || ev->key == voice_.note)
               voice_.stage = ToneVoice::Stage::Off;
            if (ev->key == -1)
               clearHeld();
            else
               setHeld(ev->key, false);
            break;
         }
         case CLAP_EVENT_NOTE_EXPRESSION: {
            auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
            if (ev->key != -1 && ev->key != voice_.note)
               break;
            if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
               voice_.bend = ev->value;
            else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
               voice_.pressure = ev->value;
            else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
               voice_.timbre = ev->value;
            break;
         }
         case CLAP_EVENT_MIDI: {
            // Plain MIDI, or MPE where each note has its own channel. Bend is +/-2 semitones, or
            // what RPN 0 sets; once an MPE configuration message (RPN 6 on channel 1) turns on a
            // zone, its note channels (2..) bend +/-48 and channel 1 bends every note. Channel
            // pressure is pressure and CC 74 is timbre, on the note's own channel.
            auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
            const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
            MidiChannel &c = midi_[channel];
            if (status == 0x90 && ev->data[2] > 0) {
               startNote(ev->data[1], ev->data[2] / 127.0f);
               if (voice_.note == ev->data[1]) {
                  voice_.channel = channel;
                  voice_.pressure = c.pressure;
                  voice_.timbre = c.timbre;
                  voice_.bend = midiBend();
               }
            } else if (status == 0x80 || status == 0x90)
               stopNote(ev->data[1]);
            else if (status == 0xE0) {
               c.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0;
               if (voice_.channel >= 0)
                  voice_.bend = midiBend();
            } else if (status == 0xD0) {
               c.pressure = ev->data[1] / 127.0;
               if (voice_.channel == channel)
                  voice_.pressure = c.pressure;
            } else if (status == 0xB0 && ev->data[1] == 74) {
               c.timbre = ev->data[2] / 127.0;
               if (voice_.channel == channel)
                  voice_.timbre = c.timbre;
            } else if (status == 0xB0 && ev->data[1] == 101)
               c.rpnMsb = ev->data[2];
            else if (status == 0xB0 && ev->data[1] == 100)
               c.rpnLsb = ev->data[2];
            else if (status == 0xB0 && ev->data[1] == 6)
               dataEntry(channel, ev->data[2]);
            else if (status == 0xB0 && ev->data[1] == 123)
               releaseAll(); // All Notes Off
            else if (status == 0xB0 && ev->data[1] == 120) {
               voice_.stage = ToneVoice::Stage::Off; // All Sound Off: silent now, no release
               clearHeld();
            }
            break;
         }
         default:
            break;
         }
      }
   }

   void resetMidi() {
      for (MidiChannel &c : midi_)
         c = MidiChannel{};
      mpeMembers_ = 0;
   }

   bool isMember(int channel) const { return channel >= 1 && channel <= mpeMembers_; }
   double bendRange(int channel) const {
      const double r = midi_[channel].bendRange;
      return r >= 0.0 ? r : isMember(channel) ? 48.0 : 2.0;
   }
   // A MIDI note's bend in semitones: its own channel's, plus channel 1's when it's in an MPE zone.
   double midiBend() const {
      const int ch = voice_.channel;
      double bend = midi_[ch].bend * bendRange(ch);
      if (isMember(ch))
         bend += midi_[0].bend * bendRange(0);
      return bend;
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
      if (voice_.channel >= 0)
         voice_.bend = midiBend();
   }

   // Copies the voice's note and expression where the panel can read them.
   void publishVoice() {
      const bool sounding = voice_.stage != ToneVoice::Stage::Off && voice_.stage != ToneVoice::Stage::Release;
      shownNote_.store(sounding ? voice_.note : -1);
      shownBend_.store(static_cast<float>(voice_.bend));
      shownPressure_.store(static_cast<float>(voice_.pressure));
      shownTimbre_.store(static_cast<float>(voice_.timbre));
   }

   void setHeld(int note, bool on) {
      if (note < 0 || note > 127)
         return;
      const uint64_t bit = uint64_t{1} << (note % 64);
      if (on)
         held_[note / 64].fetch_or(bit);
      else
         held_[note / 64].fetch_and(~bit);
   }

   void clearHeld() {
      held_[0].store(0);
      held_[1].store(0);
   }

   void releaseAll() {
      clearHeld();
      if (voice_.stage != ToneVoice::Stage::Off)
         voice_.stage = ToneVoice::Stage::Release;
   }

   // The mixer's left and right gains: level, pan and mute together.
   void mixerGains(float g[2]) const {
      const double angle = (value(kPan) + 1.0) * 0.25 * 3.14159265358979;
      const double gain = value(kMute) >= 0.5 ? 0.0 : std::pow(10.0, value(kGain) / 20.0);
      g[0] = static_cast<float>(gain * std::cos(angle) * std::sqrt(2.0));
      g[1] = static_cast<float>(gain * std::sin(angle) * std::sqrt(2.0));
   }

   void startNote(int note, float velocity) {
      if (note < 0 || note > 127)
         return;
      setHeld(note, true);
      noteCount_.fetch_add(1);
      voice_.note = note;
      voice_.channel = -1;
      voice_.velocity = velocity;
      voice_.bend = 0.0;
      voice_.pressure = 0.0;
      voice_.timbre = 0.5;
      voice_.stage = ToneVoice::Stage::Attack;
   }

   void stopNote(int note) {
      setHeld(note, false);
      if (note == voice_.note && voice_.stage != ToneVoice::Stage::Off)
         voice_.stage = ToneVoice::Stage::Release;
   }

   float nextToneSample() {
      ToneVoice &v = voice_;
      if (v.stage == ToneVoice::Stage::Off)
         return 0.0f;
      // Linear segments; each stage's param is how long a full 0..1 sweep takes.
      const auto perSample = [this](clap_id id) { return 1000.0 / (std::max(1.0, value(id)) * sampleRate_); };
      const double sustain = value(kSustain);
      switch (v.stage) {
      case ToneVoice::Stage::Attack:
         if ((v.level += perSample(kAttack)) >= 1.0) {
            v.level = 1.0;
            v.stage = ToneVoice::Stage::Decay;
         }
         break;
      case ToneVoice::Stage::Decay:
         if ((v.level -= perSample(kDecay)) <= sustain) {
            v.level = sustain;
            v.stage = ToneVoice::Stage::Sustain;
         }
         break;
      case ToneVoice::Stage::Sustain:
         v.level = sustain;
         break;
      case ToneVoice::Stage::Release:
         if ((v.level -= perSample(kRelease)) <= 0.0) {
            v.level = 0.0;
            v.stage = ToneVoice::Stage::Off;
         }
         break;
      case ToneVoice::Stage::Off:
         break;
      }

      const double note = v.note + v.bend + std::round(value(kTranspose));
      v.phase += 440.0 * std::pow(2.0, (note - 69.0) / 12.0) / sampleRate_;
      v.phase -= std::floor(v.phase);
      double wave;
      switch (static_cast<int>(value(kWave) + 0.5)) {
      case 1:
         wave = 4.0 * std::fabs(v.phase - 0.5) - 1.0;
         break;
      case 2:
         wave = 2.0 * v.phase - 1.0;
         break;
      case 3:
         wave = v.phase < 0.5 ? 1.0 : -1.0;
         break;
      default:
         wave = std::sin(2.0 * 3.14159265358979 * v.phase);
         break;
      }
      const double level = std::pow(10.0, value(kToneLevel) / 20.0);
      return static_cast<float>(wave * v.level * v.velocity * level);
   }

   // A param as the audio hears it: its value plus host modulation, clamped to its range.
   double value(clap_id id) const { return std::clamp(values_[id].load() + mods_[id].load(), kParams[id].min, kParams[id].max); }

   std::atomic<double> values_[kParamCount];
   // Host modulation, written by the audio thread.
   std::atomic<double> mods_[kParamCount];
   std::atomic<float> peak_{0.0f};
   // The mixer's gains and width as the audio applies them, gliding toward the params.
   jclap::dsp::Smoother gainL_, gainR_, width_;
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;

   // Test tone. The voice belongs to the audio thread; the rest is shared with the panel.
   double sampleRate_ = 48000.0;
   ToneVoice voice_;
   MidiChannel midi_[16];
   int mpeMembers_ = 0; // note channels in the MPE zone; 0 is plain MIDI
   std::atomic<bool> active_{false};
   jclap::core::SpscQueue<PanelNote> panelNotes_;
   std::atomic<int> shownNote_{-1};
   std::atomic<float> shownBend_{0.0f}, shownPressure_{0.0f}, shownTimbre_{0.5f};
   std::atomic<uint64_t> held_[2] = {};
   std::atomic<uint32_t> noteCount_{0};
   jclap::core::SampleFifo<4096> scope_;

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t PluginTemplate::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.plugin-template",
   "Plugin Template",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.10.4",
   "Stereo gain, pan, width and routing, with a test tone",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(PluginTemplate)
