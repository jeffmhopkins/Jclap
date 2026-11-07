// JBreath: an eight-voice MPE physical-modelling wind instrument. Each voice is a flute, clarinet,
// sax, oboe or trumpet waveguide blown by a model of the player (see models.h and voice.h): MPE
// pressure (or a breath controller on CC 2, or velocity) is the breath, slide is the embouchure,
// bend bends the bore. Poly, or mono with tonguing or slurs between notes. The voices run at 88.2 kHz
// or more and are decimated once to the host's rate.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/sample_fifo.h>
#include <jclap/dsp/denormals.h>
#include <jclap/dsp/math.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

#include "editor.h"
#include "params.h"
#include "voice.h"

namespace {

using namespace jbreath;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

// Each voice's steady tone is levelled to about -20 dBFS RMS at its tuning breath (see tools/tune.cpp),
// so a few at once leave headroom; softClip catches the rest.
constexpr float kVoiceGain = 1.0f;

// A voice and the note playing on it.
struct Slot {
   BreathVoice voice;
   // Which note this is, to match note-offs and expressions. A note id of -1 means the host gave none.
   int32_t noteId = -1;
   int16_t port = 0, channel = 0, key = -1;
   // MIDI notes take their bend, pressure and timbre from their channel (MPE gives each note its own).
   // CLAP notes get them per note, and also follow their channel's MIDI bend, pressure and CC 74, for
   // hosts that send notes as CLAP events but controllers as MIDI.
   bool midi = false;
   // A CLAP note that has had its own pressure or brightness expression uses that, not its channel's.
   bool ownPressure = false, ownTimbre = false;
   // Released while the sustain pedal was down: it keeps sounding until the pedal comes up.
   bool sustained = false;
   double bend = 0.0, pressure = 0.0, timbre = 0.5, volume = 1.0, velocity = 1.0;
   // Per-voice param modulation from the host, added to the param and its global modulation.
   double mod[kParamCount] = {};
   // When it started, for stealing the oldest.
   uint64_t started = 0;
   bool ended = true; // NOTE_END already sent
};

// Per MIDI channel expression. Bend is -1..1; channel 1 (index 0) is the MPE manager channel, whose
// bend moves every note by up to 2 semitones.
struct ChannelState {
   double bend = 0.0, pressure = 0.0, timbre = 0.5;
   bool sustain = false; // CC 64 at 64 or more
   // Pitch bend range in semitones from RPN 0, or -1 for the default (2 on a plain channel, the MPE
   // Bend Range knob on an MPE note channel).
   double bendRange = -1.0;
   // The RPN that data entry (CC 6 / 38) goes to, from CC 101 / 100; 127/127 is none.
   int rpnMsb = 127, rpnLsb = 127;
};

// A per-note modulation that arrived before its note did (in the same block, or just ahead of it).
struct PendingMod {
   int32_t noteId;
   int16_t port, channel, key;
   clap_id param;
   double amount;
};

// A key held down in a mono mode, in the order they were pressed: letting go of the sounding one
// goes back to the newest still held.
struct Held {
   int32_t noteId;
   int16_t port, channel, key;
   double velocity;
   bool midi;
};
constexpr int kMaxHeld = 16;

// State layout: 4-byte magic, uint32 version, uint32 param count, one double per param in id order,
// then uint32 name length and the preset name. A state from a version with fewer params loads them
// and leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', 'B', 'R', 'E'};
constexpr uint32_t kStateVersion = 1;

class JBreath final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JBreath(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(*this);
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      // The models were tuned at 96 kHz: run them at 88.2 kHz or more and come down once.
      sampleRate_ = sampleRate;
      shownRate_.store(sampleRate);
      oversampling_ = oversamplingFor(sampleRate);
      for (int i = 0; i < kVoiceCount; ++i) {
         // Slot holds the voice's models; keep them, reset everything else.
         Slot &s = slots_[i];
         s.voice.prepare(sampleRate * oversampling_);
         s.voice.setSeed(0x9e3779b9u * (i + 1));
         s.voice.setClock(&lfoClock_);
         s.noteId = -1;
         s.key = -1;
         s.ended = true;
      }
      decimator_.prepare(oversampling_);
      bright_.clear();
      brightDb_ = 1e9;
      for (ChannelState &c : channels_)
         c = ChannelState{};
      mpeMembers_ = 15;
      pendingCount_ = 0;
      heldCount_ = 0;
      dirty_ = true;
      publishVoices();
      return true;
   }

   void reset() override {
      for (Slot &s : slots_)
         s.voice.reset();
      for (ChannelState &c : channels_)
         c = ChannelState{};
      decimator_.reset();
      bright_.clear();
      mpeMembers_ = 15;
      pendingCount_ = 0;
      heldCount_ = 0;
   }

   // An instrument: notes in, stereo out.
   uint32_t audioPortsCount(bool isInput) const override { return isInput ? 0 : 1; }
   bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const override {
      if (!Plugin::audioPortsInfo(index, isInput, info))
         return false;
      // No input to share a buffer with.
      info->in_place_pair = CLAP_INVALID_ID;
      return true;
   }
   uint32_t notePortsCount(bool isInput) const override { return isInput ? 1 : 0; }
   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
      info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
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
      return jclap::ui::ClapGui::extensionFor<JBreath, &JBreath::gui_>(id);
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   // The knobs show global modulation; per-voice modulation differs per note, so it only sounds.
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double value) override {
      if (id >= kParamCount)
         return;
      value = std::clamp(value, kParams[id].min, kParams[id].max);
      // A stepped param (Tune, Bend Range) only takes whole values, so the host records whole ones.
      if (kParams[id].flags & CLAP_PARAM_IS_STEPPED)
         value = std::round(value);
      values_[id].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   void voices(std::vector<Voice> &out) const override {
      out.resize(kVoiceCount);
      for (int i = 0; i < kVoiceCount; ++i) {
         const Shown &v = shown_[i];
         Voice &o = out[i];
         o.note = v.note.load();
         o.hz = v.hz.load();
         o.breath = v.breath.load();
         o.pressure = v.pressure.load();
         o.timbre = v.timbre.load();
         o.level = v.level.load();
         o.releasing = v.releasing.load();
         for (int k = 0; k < kModSourceCount; ++k)
            o.sources[k] = v.sources[k].load(std::memory_order_relaxed);
         for (int k = 0; k < kModDestCount; ++k)
            o.offsets[k] = v.offsets[k].load(std::memory_order_relaxed);
      }
   }
   void readVoice(int slot, float *out, size_t count) override {
      if (slot >= 0 && slot < kVoiceCount)
         voiceScopes_[slot].read(out, count);
   }
   float outputPeak() override { return outputPeak_.exchange(0.0f); }
   double sampleRate() const override { return shownRate_.load(); }
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
      return id < kParamCount && parseParam(id, text, *value);
   }

   // Called while not processing, so only params apply; notes wait for process().
   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->type == CLAP_EVENT_PARAM_VALUE || h->type == CLAP_EVENT_PARAM_MOD)
            handleEvent(h, out);
      }
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t version = kStateVersion, count = kParamCount;
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, &version, 4) ||
          !jclap::core::writeAll(stream, &count, 4))
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
      uint32_t version = 0, count = 0;
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4) || version < 1 || version > kStateVersion ||
          !jclap::core::readAll(stream, &count, 4))
         return false;
      // Params past ours are from a newer version, and ignored.
      double v[kParamCount];
      for (size_t i = 0; i < kParamCount; ++i)
         v[i] = kParams[i].defaultValue;
      for (uint32_t i = 0; i < count; ++i) {
         double d;
         if (!jclap::core::readAll(stream, &d, sizeof(d)) || !std::isfinite(d))
            return false;
         if (i < kParamCount)
            v[i] = d;
      }
      uint32_t nameLength = 0;
      char name[256];
      if (!jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) || nameLength >= sizeof(name) ||
          !jclap::core::readAll(stream, name, nameLength))
         return false;
      presetName_.assign(name, nameLength);
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
      blockFrames_ = proc->frames_count;
      // The panel may have moved a knob since the last block.
      dirty_ = true;

      float *left = nullptr, *right = nullptr;
      if (proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32 && proc->audio_outputs[0].channel_count > 0) {
         left = proc->audio_outputs[0].data32[0];
         right = proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : nullptr;
      }

      // Events are sample accurate: render up to each one, then apply it.
      const clap_input_events_t *in = proc->in_events;
      const uint32_t eventCount = in->size(in), frames = proc->frames_count;
      const int os = oversampling_;
      float peak = 0.0f;
      uint32_t next = 0;
      for (uint32_t start = 0; start < frames;) {
         while (next < eventCount && in->get(in, next)->time <= start)
            handleEvent(in->get(in, next++), proc->out_events);
         const uint32_t end = next < eventCount ? std::min(frames, in->get(in, next)->time) : frames;
         // Only when something every voice hears has changed; a note's own events configure it.
         if (dirty_) {
            configureVoices();
            dirty_ = false;
         }
         for (uint32_t i = start; i < end; ++i) {
            lfoClock_ += 1.0 / sampleRate_;
            float fast[8] = {};
            for (int v = 0; v < kVoiceCount; ++v) {
               Slot &s = slots_[v];
               if (!s.voice.isActive())
                  continue;
               float y = 0.0f;
               for (int k = 0; k < os; ++k) {
                  y = s.voice.tick();
                  fast[k] += y;
               }
               // Every os-th sample, for the panel's loops: aliasing doesn't show in a drawing.
               voiceScopes_[v].push(y);
            }
            float sum = decimator_.process(fast);
            sum = jclap::dsp::softClip(bright_.tick(sum * kVoiceGain));
            // Where flush-to-zero is not available, a fading tail could still go subnormal.
            if (std::fabs(sum) < 1e-20f)
               sum = 0.0f;
            peak = std::max(peak, std::fabs(sum));
            if (left)
               left[i] = sum;
            if (right)
               right[i] = sum;
         }
         start = std::max(end, start + 1);
      }
      // Events at or after the last frame.
      while (next < eventCount)
         handleEvent(in->get(in, next++), proc->out_events);

      for (Slot &s : slots_)
         if (!s.voice.isActive())
            endNote(s, proc->out_events, frames > 0 ? frames - 1 : 0);
      if (peak > outputPeak_.load(std::memory_order_relaxed))
         outputPeak_.store(peak, std::memory_order_relaxed);
      publishVoices();
      blockFrames_ = 0;
      // Per-note modulation that never found its note this block isn't kept for later notes.
      pendingCount_ = 0;
      // With every voice silent, only the decimator's short tail is left; then nothing until the
      // next event.
      for (const Slot &s : slots_)
         if (s.voice.isActive()) {
            tail_ = 64;
            return CLAP_PROCESS_CONTINUE;
         }
      if (tail_ > 0) {
         tail_ = tail_ > frames ? tail_ - frames : 0;
         return CLAP_PROCESS_CONTINUE;
      }
      return CLAP_PROCESS_SLEEP;
   }

private:
   // Every sounding voice takes the current params, and its note's bend, pressure and timbre.
   void configureVoices() {
      const double bright = value(kBright);
      if (bright != brightDb_) {
         brightDb_ = bright;
         bright_.highshelf(1500.0, bright, sampleRate_);
      }
      const int mode = static_cast<int>(value(kVoiceMode) + 0.5);
      if (mode != voiceMode_) {
         // Back to poly, the keys a mono mode was holding are forgotten (they still end as usual).
         voiceMode_ = mode;
         if (mode == kPoly)
            heldCount_ = 0;
      }
      for (Slot &s : slots_) {
         if (s.voice.isActive())
            configure(s);
      }
   }

   void configure(Slot &s) {
      s.voice.configure(settings(s));
      s.voice.setPitch(pitchOf(s));
      s.voice.setBend(bendOf(s) / std::max(1.0, isMember(s.channel) ? bendRange(s.channel) : 2.0));
      s.voice.setExpression(pressureOf(s), timbreOf(s));
      s.voice.setGain(gainOf(s));
   }

   // The level (with any per-voice modulation) and the note's own volume expression. Velocity and
   // pressure are the breath, so they shape the tone as well as the loudness.
   double gainOf(const Slot &s) const { return s.volume * jclap::dsp::dbToGain(value(kLevel, &s)); }

   // A param as a voice hears it: the value, plus global and per-voice modulation, clamped.
   double value(clap_id id, const Slot *s = nullptr) const {
      const double v = values_[id].load() + mods_[id].load() + (s ? s->mod[id] : 0.0);
      return std::clamp(v, kParams[id].min, kParams[id].max);
   }
   int choice(clap_id id, const Slot &s) const { return static_cast<int>(value(id, &s) + 0.5); }

   VoiceSettings settings(const Slot &slot) const {
      const Slot *s = &slot;
      VoiceSettings vs;
      vs.model = static_cast<int>(value(kModel) + 0.5);
      vs.breath = value(kBreath, s);
      vs.embouchure = value(kEmbouchure, s);
      vs.noise = value(kNoise, s);
      vs.edge = value(kEdge, s);
      vs.kick = value(kKick, s);
      vs.attackMs = value(kAttack, s);
      vs.releaseMs = value(kRelease, s);
      vs.glideMs = value(kGlide, s);
      vs.vibRate = value(kVibRate, s);
      vs.vibDepth = value(kVibDepth, s);
      vs.vibPitch = value(kVibPitch, s);
      vs.vibDelayMs = value(kVibDelay, s);
      vs.source = static_cast<int>(value(kBreathSource) + 0.5);
      vs.breathIn = std::clamp(value(kBreathIn, s), 0.0, 1.0);
      vs.curve = value(kCurve, s);
      vs.slide = value(kSlide, s);
      vs.tune = std::round(value(kTune, s));
      vs.fine = value(kFine, s);
      vs.level = value(kLevel, s);
      for (int i = 0; i < kLfoCount; ++i) {
         vs.lfoRate[i] = value(lfoParam(i, 0), s);
         vs.lfoShape[i] = choice(lfoParam(i, 1), *s);
         vs.lfoSync[i] = choice(lfoParam(i, 2), *s);
      }
      vs.swellShape = choice(kSwellShape, *s);
      vs.swellTime = value(kSwellTime, s);
      vs.swellLoop = choice(kSwellLoop, *s) != 0;
      for (int b = 0; b < kBusCount; ++b) {
         vs.busAmount[b] = value(busParam(b, kBusAmount), s);
         vs.busMode[b] = choice(busParam(b, kBusMode), *s);
         vs.busCurve[b] = choice(busParam(b, kBusCurve), *s);
         vs.busPolarity[b] = choice(busParam(b, kBusPolarity), *s);
         vs.busLag[b] = value(busParam(b, kBusLag), s);
      }
      // The routes in use: a source, a destination and an amount that isn't zero.
      for (int r = 0; r < kRouteCount; ++r) {
         const int src = choice(routeParam(r, kRouteSource), *s), dst = choice(routeParam(r, kRouteDestination), *s);
         const double amount = value(routeParam(r, kRouteAmount), s);
         if (src <= kSrcOff || src >= kModSourceCount || dst < 0 || dst >= kModDestCount || amount == 0.0)
            continue;
         vs.routes[vs.routeCount++] = {src, dst, static_cast<float>(amount)};
      }
      return vs;
   }

   // An MPE lower zone: channel 1 manages, channels 2..mpeMembers_+1 each carry their own notes.
   // It's on (all 15 channels) until an MPE Configuration Message says otherwise; with a zone of 0
   // every channel is a plain MIDI channel.
   bool isMember(int channel) const { return channel >= 1 && channel <= mpeMembers_; }
   bool zoneOn() const { return mpeMembers_ > 0; }
   double bendRange(int channel) const {
      const double r = channels_[channel].bendRange;
      return r >= 0.0 ? r : isMember(channel) ? value(kBendRange) : 2.0;
   }

   double pitchOf(const Slot &s) const { return s.key + std::round(value(kTune, &s)) + value(kFine, &s) / 100.0 + bendOf(s); }
   // In a zone the manager channel's bend moves every note, and a note channel's moves its own notes;
   // a plain channel's bend moves its own notes.
   double bendOf(const Slot &s) const {
      if (isMember(s.channel))
         return s.bend + channels_[0].bend * bendRange(0) + channels_[s.channel].bend * bendRange(s.channel);
      return s.bend + channels_[s.channel].bend * bendRange(s.channel);
   }
   double pressureOf(const Slot &s) const { return s.ownPressure ? s.pressure : channels_[s.channel].pressure; }
   double timbreOf(const Slot &s) const { return s.ownTimbre ? s.timbre : channels_[s.channel].timbre; }

   // The sustain pedal holds a note when it's down on the note's channel or the manager channel.
   bool pedalDown(const Slot &s) const {
      return channels_[s.channel].sustain || (isMember(s.channel) && channels_[0].sustain);
   }

   // The channels a channel-wide message on `channel` reaches: its own, or every channel in the zone
   // when it comes on the manager channel.
   bool reaches(int channel, const Slot &s) const {
      return s.channel == channel || (channel == 0 && zoneOn() && isMember(s.channel));
   }

   // Data entry for the RPN picked with CC 101 / 100. RPN 0 is the channel's pitch bend range
   // (semitones on CC 6, cents on CC 38). RPN 6 on channel 1 is the MPE Configuration Message: how
   // many channels after it carry notes, with their bend ranges back to the MPE defaults.
   void dataEntry(int channel, bool msb, int v) {
      ChannelState &c = channels_[channel];
      if (c.rpnMsb != 0)
         return;
      if (c.rpnLsb == 0) {
         const double semitones = msb ? v : std::floor(std::max(0.0, c.bendRange));
         const double cents = msb ? 0.0 : v;
         c.bendRange = std::min(96.0, semitones + cents / 100.0);
         dirty_ = true;
      }
      else if (c.rpnLsb == 6 && channel == 0 && msb) {
         mpeMembers_ = std::min(v, 15);
         for (ChannelState &m : channels_)
            m.bendRange = -1.0;
         dirty_ = true;
      }
   }

   // Lets go of the notes the pedal is holding on the channels `channel` reaches, unless another
   // pedal still holds them.
   void releasePedalled(int channel, int16_t port) {
      for (Slot &s : slots_)
         if (s.sustained && s.voice.isActive() && s.port == port && reaches(channel, s) && !pedalDown(s)) {
            s.sustained = false;
            s.voice.release();
         }
   }
   // A host note-off: let go now, or when the pedal comes up.
   void releaseHost(Slot &s) {
      if (pedalDown(s))
         s.sustained = true;
      else
         s.voice.release(liftVelocity_);
   }

   void handleEvent(const clap_event_header_t *h, const clap_output_events_t *out) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      switch (h->type) {
      case CLAP_EVENT_PARAM_VALUE: {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         // A NaN would get past the clamp and silence the synth for good.
         if (ev->param_id < kParamCount && std::isfinite(ev->value)) {
            values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
            dirty_ = true;
         }
         break;
      }
      case CLAP_EVENT_PARAM_MOD: {
         // An amount added to the param: for every voice when no note is named, else for the voices
         // it names (Bitwig's polyphonic modulators send one per note id).
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         if (ev->param_id >= kParamCount || !std::isfinite(ev->amount))
            break;
         dirty_ = true;
         if (ev->note_id == -1 && ev->key == -1 && ev->channel == -1) {
            mods_[ev->param_id].store(ev->amount);
            break;
         }
         bool found = false;
         for (Slot &s : slots_)
            if (hostMatches(s, ev->note_id, ev->port_index, ev->channel, ev->key)) {
               s.mod[ev->param_id] = ev->amount;
               found = true;
            }
         // Sent with or just before its note-on (as hosts may for a note's starting value): kept
         // until the end of the block for the note to pick up.
         if (!found && pendingCount_ < kMaxPending)
            pending_[pendingCount_++] = {ev->note_id, ev->port_index, ev->channel, ev->key, ev->param_id, ev->amount};
         break;
      }
      case CLAP_EVENT_NOTE_ON: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         startNote(ev->note_id, ev->port_index, ev->channel, ev->key, ev->velocity, false, out, h->time);
         break;
      }
      case CLAP_EVENT_NOTE_OFF:
      case CLAP_EVENT_NOTE_CHOKE: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         if (h->type == CLAP_EVENT_NOTE_OFF) {
            liftVelocity_ = ev->velocity;
            noteOff(ev->note_id, ev->port_index, ev->channel, ev->key, false, out, h->time);
            break;
         }
         // A choke naming nothing is a panic.
         const bool all = ev->note_id == -1 && ev->key == -1 && ev->channel == -1;
         if (all)
            heldCount_ = 0;
         else
            forgetHeld(ev->note_id, ev->port_index, ev->channel, ev->key);
         for (Slot &s : slots_) {
            if (!(all ? s.voice.isActive() : hostMatches(s, ev->note_id, ev->port_index, ev->channel, ev->key)))
               continue;
            s.voice.reset();
            endNote(s, out, h->time);
         }
         break;
      }
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         if (!std::isfinite(ev->value))
            break;
         for (Slot &s : slots_) {
            if (!hostMatches(s, ev->note_id, ev->port_index, ev->channel, ev->key))
               continue;
            switch (ev->expression_id) {
            // Clamped to the ranges CLAP gives them.
            case CLAP_NOTE_EXPRESSION_TUNING:
               s.bend = std::clamp(ev->value, -120.0, 120.0);
               break;
            case CLAP_NOTE_EXPRESSION_PRESSURE:
               s.pressure = std::clamp(ev->value, 0.0, 1.0);
               s.ownPressure = true;
               break;
            case CLAP_NOTE_EXPRESSION_BRIGHTNESS:
               s.timbre = std::clamp(ev->value, 0.0, 1.0);
               s.ownTimbre = true;
               break;
            case CLAP_NOTE_EXPRESSION_VOLUME:
               s.volume = std::clamp(ev->value, 0.0, 4.0);
               break;
            default:
               break;
            }
            configure(s);
         }
         break;
      }
      case CLAP_EVENT_MIDI: {
         // MPE gives each note its own channel (2..16), so its bend, channel pressure and CC 74
         // (timbre) are that note's alone. Plain MIDI on channel 1 bends every note by +/-2. CLAP
         // notes on the channel follow these too (see Slot::midi).
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         ChannelState &c = channels_[channel];
         if (status == 0x90 && ev->data[2] > 0) {
            startNote(-1, ev->port_index, channel, ev->data[1], ev->data[2] / 127.0, true, out, h->time);
         } else if (status == 0x80 || status == 0x90) {
            liftVelocity_ = status == 0x80 ? ev->data[2] / 127.0 : 0.5;
            noteOff(-1, ev->port_index, channel, ev->data[1], true, out, h->time);
         } else if (status == 0xE0) {
            c.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0;
            dirty_ = true;
         } else if (status == 0xD0) {
            c.pressure = ev->data[1] / 127.0;
            dirty_ = true;
         } else if (status == 0xA0) {
            // Polyphonic aftertouch: that one MIDI note's pressure.
            for (Slot &s : slots_)
               if (s.midi && matches(s, -1, ev->port_index, channel, ev->data[1])) {
                  s.pressure = ev->data[2] / 127.0;
                  s.ownPressure = true;
                  configure(s);
               }
         } else if (status == 0xB0 && ev->data[1] == 2) {
            // A breath controller: the channel's breath, as channel pressure would be.
            c.pressure = ev->data[2] / 127.0;
            dirty_ = true;
         } else if (status == 0xB0 && ev->data[1] == 74) {
            c.timbre = ev->data[2] / 127.0;
            dirty_ = true;
         } else if (status == 0xB0 && ev->data[1] == 64) {
            // Sustain: up lets go of the notes it was holding, unless the other pedal that applies
            // (this channel's or the manager channel's) is still down.
            c.sustain = ev->data[2] >= 64;
            if (!c.sustain)
               releasePedalled(channel, ev->port_index);
         } else if (status == 0xB0 && (ev->data[1] == 101 || ev->data[1] == 100)) {
            (ev->data[1] == 101 ? c.rpnMsb : c.rpnLsb) = ev->data[2];
         } else if (status == 0xB0 && (ev->data[1] == 6 || ev->data[1] == 38)) {
            dataEntry(channel, ev->data[1] == 6, ev->data[2]);
         } else if (status == 0xB0 && ev->data[1] == 121) {
            // Reset All Controllers: bend, pressure, CC 74 and the pedal back to rest, on the
            // channels it reaches, and the notes the pedal held let go.
            for (int ch = 0; ch < 16; ++ch)
               if (ch == channel || (channel == 0 && zoneOn() && isMember(ch))) {
                  ChannelState &r = channels_[ch];
                  r.bend = r.pressure = 0.0;
                  r.timbre = 0.5;
                  r.sustain = false;
                  r.rpnMsb = r.rpnLsb = 127;
               }
            releasePedalled(channel, ev->port_index);
            dirty_ = true;
         } else if (status == 0xB0 && (ev->data[1] == 120 || ev->data[1] == 123)) {
            // All sound off cuts, all notes off releases: the host's notes on that channel, or on
            // every channel when it comes on the manager channel (1), as for the MPE zone. Both lift
            // the pedal too, so a stop mid-pedal doesn't leave every later note held.
            for (int ch = 0; ch < 16; ++ch)
               if (ch == channel || (channel == 0 && zoneOn() && isMember(ch)))
                  channels_[ch].sustain = false;
            heldCount_ = 0;
            for (Slot &s : slots_) {
               if (!s.voice.isActive() || s.port != ev->port_index || !reaches(channel, s))
                  continue;
               // Both also let go of notes the pedal is holding.
               s.sustained = false;
               if (ev->data[1] == 120) {
                  s.voice.reset();
                  endNote(s, out, h->time);
               } else {
                  s.voice.release();
               }
            }
         }
         break;
      }
      default:
         break;
      }
   }

   static bool matches(const Slot &s, int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      if (!s.voice.isActive())
         return false;
      if (noteId != -1)
         return s.noteId == noteId;
      return (key == -1 || s.key == key) && (channel == -1 || s.channel == channel) && (port == -1 || s.port == port);
   }

   static bool hostMatches(const Slot &s, int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      return matches(s, noteId, port, channel, key);
   }

   bool mono() const { return static_cast<int>(value(kVoiceMode) + 0.5) != kPoly; }

   static bool heldMatches(const Held &h, int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      if (noteId != -1)
         return h.noteId == noteId;
      return (key == -1 || h.key == key) && (channel == -1 || h.channel == channel) && (port == -1 || h.port == port);
   }
   void forgetHeld(int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      int n = 0;
      for (int i = 0; i < heldCount_; ++i)
         if (!heldMatches(held_[i], noteId, port, channel, key))
            held_[n++] = held_[i];
      heldCount_ = n;
   }

   // A key let go. In a mono mode, letting go of the sounding note goes back to the newest key
   // still held (slurred in Legato, tongued in Mono); with none, the note ends.
   void noteOff(int32_t noteId, int16_t port, int16_t channel, int16_t key, bool midiOnly,
                const clap_output_events_t *out, uint32_t time) {
      if (mono()) {
         forgetHeld(noteId, port, channel, key);
         Slot &s = slots_[0];
         if ((midiOnly && !s.midi) || !hostMatches(s, noteId, port, channel, key) || s.voice.isReleasing())
            return;
         if (heldCount_ > 0) {
            const Held h = held_[heldCount_ - 1];
            take(s, h.noteId, h.port, h.channel, h.key, h.velocity, h.midi, out, time, true);
            playMono(s);
         } else {
            releaseHost(s);
         }
         return;
      }
      for (Slot &s : slots_)
         if ((!midiOnly || s.midi) && hostMatches(s, noteId, port, channel, key))
            releaseHost(s);
   }

   // A mono note onto the one voice: started from silence, or slurred or tongued from the last.
   void playMono(Slot &s) {
      configure(s);
      if (!s.voice.isActive() || s.voice.isReleasing())
         s.voice.start(pitchOf(s), s.velocity, gainOf(s));
      else if (static_cast<int>(value(kVoiceMode) + 0.5) == kLegato)
         s.voice.slur(pitchOf(s), s.velocity);
      else
         s.voice.retongue(pitchOf(s), s.velocity);
   }

   // Gives slot `s` to a note. `keepExpression` keeps the last note's pressure and slide, so a mono
   // line doesn't drop its breath between notes before the new note's own pressure arrives.
   void take(Slot &s, int32_t noteId, int16_t port, int16_t channel, int16_t key, double velocity, bool midi,
             const clap_output_events_t *out, uint32_t time, bool keepExpression) {
      // A different note taking this voice has ended, as far as the host is concerned. (MIDI notes
      // have no id, so compare where they are too.) So has one that fell silent earlier in this
      // block, before the end-of-block scan could report it.
      if (!s.voice.isActive() || s.noteId != noteId || s.key != key || s.channel != channel || s.port != port)
         endNote(s, out, time);
      const bool keep = keepExpression && s.voice.isActive() && !s.voice.isReleasing();
      const double pressure = pressureOf(s), timbre = timbreOf(s);
      s.noteId = noteId;
      s.port = port;
      s.channel = channel;
      s.key = key;
      s.midi = midi;
      s.sustained = false;
      s.bend = 0.0;
      s.volume = 1.0;
      s.velocity = velocity;
      s.started = ++clock_;
      s.ended = false;
      if (keep) {
         s.pressure = pressure;
         s.timbre = timbre;
         s.ownPressure = s.ownTimbre = true;
      } else {
         s.ownPressure = s.ownTimbre = false;
         s.pressure = 0.0;
         s.timbre = 0.5;
      }
      for (double &m : s.mod)
         m = 0.0;
      for (int i = 0; i < pendingCount_; ++i) {
         const PendingMod &m = pending_[i];
         if (m.noteId != -1 ? m.noteId == noteId
                            : (m.key == -1 || m.key == key) && (m.channel == -1 || m.channel == channel) &&
                                  (m.port == -1 || m.port == port))
            s.mod[m.param] = m.amount;
      }
   }

   void startNote(int32_t noteId, int16_t port, int16_t channel, int16_t key, double velocity, bool midi,
                  const clap_output_events_t *out, uint32_t time) {
      // A CLAP note at velocity 0 is still a note (MIDI's "velocity 0 is note off" was handled before
      // this), so it plays, as softly as velocity makes it, and ends like any other.
      if (key < 0 || key > 127 || channel < 0 || channel > 15 || !std::isfinite(velocity))
         return;
      velocity = std::clamp(velocity, 0.0, 1.0);
      if (mono()) {
         forgetHeld(-1, port, channel, key);
         if (heldCount_ == kMaxHeld) {
            std::copy(held_ + 1, held_ + kMaxHeld, held_);
            --heldCount_;
         }
         held_[heldCount_++] = {noteId, port, channel, key, velocity, midi};
         Slot &s = slots_[0];
         take(s, noteId, port, channel, key, velocity, midi, out, time, true);
         playMono(s);
         return;
      }
      Slot &s = pickSlot(port, channel, key);
      take(s, noteId, port, channel, key, velocity, midi, out, time, false);
      configure(s);
      s.voice.start(pitchOf(s), velocity, gainOf(s));
   }

   // The same note if it is still sounding, else a free voice, else the quietest releasing (or
   // pedal-held) one, else the oldest.
   Slot &pickSlot(int16_t port, int16_t channel, int16_t key) {
      for (Slot &s : slots_)
         if (s.voice.isActive() && s.key == key && s.channel == channel && s.port == port)
            return s;
      for (Slot &s : slots_)
         if (!s.voice.isActive())
            return s;
      Slot *best = nullptr;
      // Notes only the sustain pedal is holding count as let go: steal those before a key still down.
      for (Slot &s : slots_)
         if ((s.voice.isReleasing() || s.sustained) && (!best || s.voice.level() < best->voice.level()))
            best = &s;
      if (best)
         return *best;
      best = &slots_[0];
      for (Slot &s : slots_)
         if (s.started < best->started)
            best = &s;
      return *best;
   }

   // Tells the host a note has finished, once.
   void endNote(Slot &s, const clap_output_events_t *out, uint32_t time) {
      if (s.ended)
         return;
      s.ended = true;
      if (!out)
         return;
      // A host event timed past the block still ends its note inside it.
      time = std::min(time, blockFrames_ > 0 ? blockFrames_ - 1 : 0);
      clap_event_note_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0};
      ev.note_id = s.noteId;
      ev.port_index = s.port;
      ev.channel = s.channel;
      ev.key = s.key;
      out->try_push(out, &ev.header);
   }

   // Copies the voices where the panel can read them.
   void publishVoices() {
      for (int i = 0; i < kVoiceCount; ++i) {
         const Slot &s = slots_[i];
         Shown &v = shown_[i];
         const bool on = s.voice.isActive();
         v.note.store(on ? s.key : -1);
         v.hz.store(on ? static_cast<float>(s.voice.hz()) : 0.0f);
         v.breath.store(on ? s.voice.breath() : 0.0f);
         v.pressure.store(on ? static_cast<float>(pressureOf(s)) : 0.0f);
         v.timbre.store(on ? static_cast<float>(timbreOf(s)) : 0.5f);
         v.level.store(on ? s.voice.level() : 0.0f);
         v.releasing.store(on && s.voice.isReleasing());
         const float *src = s.voice.sources(), *off = s.voice.offsets();
         for (int k = 0; k < kModSourceCount; ++k)
            v.sources[k].store(on ? src[k] : 0.0f, std::memory_order_relaxed);
         for (int k = 0; k < kModDestCount; ++k)
            v.offsets[k].store(on ? off[k] : 0.0f, std::memory_order_relaxed);
      }
   }

   std::atomic<double> values_[kParamCount];
   // Global modulation from the host, written by the audio thread.
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // The preset last loaded or saved (main thread).
   std::string presetName_ = "Init";

   // Audio thread.
   Slot slots_[kVoiceCount];
   ChannelState channels_[16];
   Held held_[kMaxHeld] = {};
   int heldCount_ = 0;
   int voiceMode_ = kPoly;
   double sampleRate_ = 48000.0;
   int oversampling_ = 2;
   Decimator decimator_;
   Biquad bright_;
   double brightDb_ = 1e9;
   uint32_t tail_ = 0;
   uint64_t clock_ = 0;

   // Shared with the panel.
   struct Shown {
      std::atomic<int> note{-1};
      std::atomic<float> hz{0.0f}, breath{0.0f}, pressure{0.0f}, timbre{0.5f}, level{0.0f};
      std::atomic<bool> releasing{false};
      std::atomic<float> sources[kModSourceCount] = {};
      std::atomic<float> offsets[kModDestCount] = {};
   };
   Shown shown_[kVoiceCount];
   jclap::core::SampleFifo<2048> voiceScopes_[kVoiceCount];
   std::atomic<float> outputPeak_{0.0f};
   std::atomic<double> shownRate_{48000.0};
   // The MPE lower zone's size (see isMember), set by an MPE Configuration Message.
   int mpeMembers_ = 15;
   static constexpr int kMaxPending = 32;
   PendingMod pending_[kMaxPending];
   int pendingCount_ = 0;
   // Something every voice hears changed since they were last configured.
   bool dirty_ = true;
   // The note-off velocity being handled (the Lift source), and the free LFOs' clock in seconds.
   double liftVelocity_ = 0.5, lfoClock_ = 0.0;
   // This block's length, while it runs; 0 outside a block.
   uint32_t blockFrames_ = 0;

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JBreath::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jbreath",
   "JBreath",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "An eight-voice MPE physical-modelling wind instrument: flute, clarinet, sax, oboe and trumpet",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JBreath)
