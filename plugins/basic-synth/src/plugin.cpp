// Basic Synth: an eight-voice MPE synth built from jclap::dsp. Each voice has an oscillator, a filter
// and a VCA, two envelopes that can each go to the filter, VCA and pitch, and an LFO (see voice.h).
// Notes arrive as CLAP notes with per-note expressions, as MPE MIDI, or as plain MIDI; the panel's
// keyboard and note grid play it too.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/sample_fifo.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/dsp/denormals.h>
#include <jclap/dsp/envelope.h>
#include <jclap/dsp/math.h>
#include <jclap/dsp/vca.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "editor.h"
#include "params.h"
#include "voice.h"

namespace {

using namespace basic_synth;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr int kVoiceCount = 8;
// Each voice at -6 dB, so a few at once leave headroom; softClip catches the rest.
constexpr float kVoiceGain = 0.5f;

// A note from the panel to the audio thread.
struct PanelNote {
   enum class Kind : uint8_t { On, Expression, Off } kind;
   int8_t note;
   float velocity, bend, pressure, timbre;
};

// A voice and the note playing on it.
struct Slot {
   SynthVoice voice;
   // Which note this is, to match note-offs and expressions. A note id of -1 means the host gave none.
   int32_t noteId = -1;
   int16_t port = 0, channel = 0, key = -1;
   // MIDI notes take their bend, pressure and timbre from their channel (MPE gives each note its own);
   // CLAP notes and the panel's get them per note.
   bool midi = false;
   // Played on the panel: the host never sent it, so it hears no NOTE_END for it, and the panel's
   // note-offs and expressions touch only these.
   bool panel = false;
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
};

// State layout: 4-byte magic, uint32 version, uint32 param count, then one double per param in id
// order. A state from a version with fewer params loads them and leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', 'B', 'S', 'Y'};
constexpr uint32_t kStateVersion = 1;

class BasicSynth final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit BasicSynth(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      for (int i = 0; i < kVoiceCount; ++i) {
         slots_[i] = Slot{};
         slots_[i].voice.setSampleRate(sampleRate);
         slots_[i].voice.setSeed(0x9e3779b9u * (i + 1));
      }
      for (ChannelState &c : channels_)
         c = ChannelState{};
      for (int e = 0; e < kEnvCount; ++e) {
         loopEnv_[e].setSampleRate(sampleRate);
         loopEnv_[e].reset();
         looping_[e] = false;
      }
      output_.setSampleRate(sampleRate);
      output_.setGainDb(value(kLevel));
      output_.reset();
      publishVoices();
      return true;
   }

   void reset() override {
      for (Slot &s : slots_)
         s.voice.reset();
      for (ChannelState &c : channels_)
         c = ChannelState{};
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
      return jclap::ui::ClapGui::extensionFor<BasicSynth, &BasicSynth::gui_>(id);
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
      values_[id].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   void readScope(float *out, size_t count) override { scope_.read(out, count); }
   void noteOn(int note, float velocity) override {
      panelNotes_.push({PanelNote::Kind::On, static_cast<int8_t>(note), std::max(0.01f, velocity), 0, 0, 0.5f});
   }
   void noteExpression(int note, float bend, float pressure, float timbre) override {
      panelNotes_.push({PanelNote::Kind::Expression, static_cast<int8_t>(note), 0, bend, pressure, timbre});
   }
   void noteOff(int note) override { panelNotes_.push({PanelNote::Kind::Off, static_cast<int8_t>(note), 0, 0, 0, 0}); }
   void voices(std::vector<Voice> &out) const override {
      out.clear();
      for (const Shown &s : shown_) {
         const int note = s.note.load();
         if (note >= 0)
            out.push_back({note, s.bend.load(), s.pressure.load(), s.timbre.load()});
      }
   }

   bool lfo(float &value) const override {
      if (!lfoShown_.load())
         return false;
      value = lfoValue_.load();
      return true;
   }
   void envelopes(int index, std::vector<EnvState> &out) const override {
      out.clear();
      if (index < 0 || index >= kEnvCount)
         return;
      // Looping, there is one shared cycle to show.
      if (loopShown_[index].stage.load() >= 0) {
         out.push_back({loopShown_[index].stage.load(), loopShown_[index].level.load(), 1.0f});
         return;
      }
      for (const Shown &s : shown_) {
         const int stage = s.envStage[index].load();
         if (stage >= 0)
            out.push_back({stage, s.envLevel[index].load(), s.envFrom[index].load()});
      }
   }

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
      return true;
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t version = 0, count = 0;
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4) || version != kStateVersion ||
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
      PanelNote p;
      while (panelNotes_.pop(p))
         handlePanelNote(p, proc->out_events);

      float *left = nullptr, *right = nullptr;
      if (proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32 && proc->audio_outputs[0].channel_count > 0) {
         left = proc->audio_outputs[0].data32[0];
         right = proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : nullptr;
      }

      // Events are sample accurate: render up to each one, then apply it.
      const clap_input_events_t *in = proc->in_events;
      const uint32_t eventCount = in->size(in), frames = proc->frames_count;
      uint32_t next = 0;
      for (uint32_t start = 0; start < frames;) {
         while (next < eventCount && in->get(in, next)->time <= start)
            handleEvent(in->get(in, next++), proc->out_events);
         const uint32_t end = next < eventCount ? std::min(frames, in->get(in, next)->time) : frames;
         configureVoices();
         for (uint32_t i = start; i < end; ++i) {
            double shared[kEnvCount];
            for (int e = 0; e < kEnvCount; ++e)
               shared[e] = looping_[e] ? loopEnv_[e].process() : 0.0;
            float sum = 0.0f;
            for (Slot &s : slots_)
               sum += s.voice.process(shared);
            sum = jclap::dsp::softClip(output_.process(sum * kVoiceGain));
            // Where flush-to-zero is not available, a fading tail could still go subnormal.
            if (std::fabs(sum) < 1e-20f)
               sum = 0.0f;
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
      if (left)
         scope_.write(left, frames);
      publishVoices();
      return CLAP_PROCESS_CONTINUE;
   }

private:
   // Every sounding voice takes the current params, and its note's bend, pressure and timbre.
   void configureVoices() {
      output_.setGainDb(value(kLevel));
      // A looping envelope is one cycle every voice shares, free running from the moment its loop
      // switch went on.
      for (int e = 0; e < kEnvCount; ++e) {
         const bool on = value(envParam(e, kEnv1Loop)) >= 0.5;
         jclap::dsp::Envelope &env = loopEnv_[e];
         env.setAttack(value(envParam(e, kEnv1Attack)) / 1000.0);
         env.setDecay(value(envParam(e, kEnv1Decay)) / 1000.0);
         env.setSustain(value(envParam(e, kEnv1Sustain)));
         env.setRelease(value(envParam(e, kEnv1Release)) / 1000.0);
         env.setLoop(true);
         if (on && !looping_[e]) {
            env.reset();
            env.gateOn();
         }
         looping_[e] = on;
      }
      for (Slot &s : slots_) {
         if (s.voice.isActive())
            configure(s);
      }
   }

   void configure(Slot &s) {
      s.voice.configure(settings(s));
      s.voice.setPitch(pitchOf(s));
      s.voice.setExpression(pressureOf(s), timbreOf(s));
      s.voice.setGain(gainOf(s));
   }

   // The output VCA takes the level; a voice's own level modulation scales just that voice. Velocity
   // takes away up to its amount: at 0 every note is as loud as the hardest.
   double gainOf(const Slot &s) const {
      const double velocity = 1.0 - value(kVelocityToVca, &s) * (1.0 - s.velocity);
      return velocity * s.volume * jclap::dsp::dbToGain(s.mod[kLevel]);
   }

   // A param as a voice hears it: the value, plus global and per-voice modulation, clamped.
   double value(clap_id id, const Slot *s = nullptr) const {
      const double v = values_[id].load() + mods_[id].load() + (s ? s->mod[id] : 0.0);
      return std::clamp(v, kParams[id].min, kParams[id].max);
   }
   int choice(clap_id id, const Slot &s) const { return static_cast<int>(value(id, &s) + 0.5); }

   VoiceSettings settings(const Slot &slot) const {
      const Slot *s = &slot;
      VoiceSettings vs;
      vs.wave = static_cast<jclap::dsp::Oscillator::Wave>(choice(kWave, slot));
      vs.pulseWidth = value(kPulseWidth, s);
      vs.dual = value(kDual, s) >= 0.5;
      vs.detune = detuneSemitones(value(kDetune, s));
      vs.filterMode = static_cast<jclap::dsp::Filter::Mode>(choice(kFilterMode, slot));
      vs.slope = static_cast<jclap::dsp::Filter::Slope>(choice(kSlope, slot) + 1);
      vs.cutoffHz = cutoffHz(value(kCutoff, s));
      vs.resonance = value(kResonance, s);
      vs.keyTrack = value(kKeyTrack, s);
      vs.velocityOctaves = value(kVelocityToFilter, s) * slot.velocity;
      vs.pressureAmount = value(kPressureAmount, s);
      vs.timbreAmount = value(kTimbreAmount, s);
      for (int i = 0; i < kEnvCount; ++i) {
         VoiceSettings::Env &e = vs.env[i];
         e.attack = value(envParam(i, kEnv1Attack), s) / 1000.0;
         e.decay = value(envParam(i, kEnv1Decay), s) / 1000.0;
         e.sustain = value(envParam(i, kEnv1Sustain), s);
         e.release = value(envParam(i, kEnv1Release), s) / 1000.0;
         e.loop = value(envParam(i, kEnv1Loop), s) >= 0.5;
         e.toFilter = value(envParam(i, kEnv1ToFilter), s);
         e.toVca = value(envParam(i, kEnv1ToVca), s);
         e.toPitch = value(envParam(i, kEnv1ToPitch), s);
      }
      vs.lfoShape = static_cast<jclap::dsp::Lfo::Shape>(choice(kLfoShape, slot));
      vs.lfoRate = lfoHz(value(kLfoRate, s));
      vs.lfoToCutoff = value(kLfoToCutoff, s);
      vs.lfoToVca = value(kLfoToVca, s);
      vs.lfoToPulseWidth = value(kLfoToPulseWidth, s);
      return vs;
   }

   double pitchOf(const Slot &s) const {
      double bend = s.bend;
      if (s.midi) {
         // The manager channel's bend moves every note; a note channel's moves its own note.
         bend = channels_[0].bend * 2.0;
         if (s.channel > 0)
            bend += channels_[s.channel].bend * value(kBendRange);
      }
      return s.key + std::round(value(kTune, &s)) + bend;
   }
   double pressureOf(const Slot &s) const { return s.midi ? channels_[s.channel].pressure : s.pressure; }
   double timbreOf(const Slot &s) const { return s.midi ? channels_[s.channel].timbre : s.timbre; }

   void handleEvent(const clap_event_header_t *h, const clap_output_events_t *out) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      switch (h->type) {
      case CLAP_EVENT_PARAM_VALUE: {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         // A NaN would get past the clamp and silence the synth for good.
         if (ev->param_id < kParamCount && std::isfinite(ev->value))
            values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
         break;
      }
      case CLAP_EVENT_PARAM_MOD: {
         // An amount added to the param: for every voice when no note is named, else for the voices
         // it names (Bitwig's polyphonic modulators send one per note id).
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         if (ev->param_id >= kParamCount || !std::isfinite(ev->amount))
            break;
         if (ev->note_id == -1 && ev->key == -1 && ev->channel == -1) {
            mods_[ev->param_id].store(ev->amount);
            break;
         }
         for (Slot &s : slots_)
            if (matches(s, ev->note_id, ev->port_index, ev->channel, ev->key))
               s.mod[ev->param_id] = ev->amount;
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
         for (Slot &s : slots_) {
            if (!matches(s, ev->note_id, ev->port_index, ev->channel, ev->key))
               continue;
            if (h->type == CLAP_EVENT_NOTE_CHOKE) {
               s.voice.reset();
               endNote(s, out, h->time);
            } else {
               s.voice.release();
            }
         }
         break;
      }
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         if (!std::isfinite(ev->value))
            break;
         for (Slot &s : slots_) {
            if (!matches(s, ev->note_id, ev->port_index, ev->channel, ev->key))
               continue;
            switch (ev->expression_id) {
            // Clamped to the ranges CLAP gives them.
            case CLAP_NOTE_EXPRESSION_TUNING:
               s.bend = std::clamp(ev->value, -120.0, 120.0);
               break;
            case CLAP_NOTE_EXPRESSION_PRESSURE:
               s.pressure = std::clamp(ev->value, 0.0, 1.0);
               break;
            case CLAP_NOTE_EXPRESSION_BRIGHTNESS:
               s.timbre = std::clamp(ev->value, 0.0, 1.0);
               break;
            case CLAP_NOTE_EXPRESSION_VOLUME:
               s.volume = std::clamp(ev->value, 0.0, 4.0);
               break;
            default:
               break;
            }
         }
         break;
      }
      case CLAP_EVENT_MIDI: {
         // MPE gives each note its own channel (2..16), so its bend, channel pressure and CC 74
         // (timbre) are that note's alone. Plain MIDI on channel 1 bends every note by +/-2.
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         ChannelState &c = channels_[channel];
         if (status == 0x90 && ev->data[2] > 0) {
            startNote(-1, ev->port_index, channel, ev->data[1], ev->data[2] / 127.0, true, out, h->time);
         } else if (status == 0x80 || status == 0x90) {
            for (Slot &s : slots_)
               if (s.midi && matches(s, -1, ev->port_index, channel, ev->data[1]))
                  s.voice.release();
         } else if (status == 0xE0) {
            c.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0;
         } else if (status == 0xD0) {
            c.pressure = ev->data[1] / 127.0;
         } else if (status == 0xB0 && ev->data[1] == 74) {
            c.timbre = ev->data[2] / 127.0;
         } else if (status == 0xB0 && (ev->data[1] == 120 || ev->data[1] == 123)) {
            // All sound off cuts, all notes off releases.
            for (Slot &s : slots_)
               ev->data[1] == 120 ? s.voice.reset() : s.voice.release();
         }
         break;
      }
      default:
         break;
      }
   }

   void handlePanelNote(const PanelNote &p, const clap_output_events_t *out) {
      if (p.kind == PanelNote::Kind::On) {
         // Stealing a host note's voice still tells the host that note ended.
         startNote(-1, 0, 0, p.note, p.velocity, false, out, 0, true);
         return;
      }
      for (Slot &s : slots_) {
         if (!s.panel || !matches(s, -1, 0, 0, p.note))
            continue;
         if (p.kind == PanelNote::Kind::Off) {
            s.voice.release();
         } else {
            s.bend = p.bend;
            s.pressure = p.pressure;
            s.timbre = p.timbre;
         }
      }
   }

   static bool matches(const Slot &s, int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      if (!s.voice.isActive())
         return false;
      if (noteId != -1)
         return s.noteId == noteId;
      return (key == -1 || s.key == key) && (channel == -1 || s.channel == channel) && (port == -1 || s.port == port);
   }

   void startNote(int32_t noteId, int16_t port, int16_t channel, int16_t key, double velocity, bool midi,
                  const clap_output_events_t *out, uint32_t time, bool panel = false) {
      if (key < 0 || key > 127 || channel < 0 || channel > 15)
         return;
      if (velocity <= 0.0) {
         for (Slot &s : slots_)
            if (matches(s, noteId, port, channel, key))
               s.voice.release();
         return;
      }
      Slot &s = pickSlot(port, channel, key, panel);
      // A different note taking this voice has ended, as far as the host is concerned. (MIDI and
      // panel notes have no id, so compare where they are too.)
      if (s.voice.isActive() &&
          (s.noteId != noteId || s.key != key || s.channel != channel || s.port != port || s.panel != panel))
         endNote(s, out, time);
      s.noteId = noteId;
      s.port = port;
      s.channel = channel;
      s.key = key;
      s.midi = midi;
      s.panel = panel;
      s.bend = 0.0;
      s.pressure = 0.0;
      s.timbre = 0.5;
      s.volume = 1.0;
      s.velocity = velocity;
      s.started = ++clock_;
      s.ended = false;
      for (double &m : s.mod)
         m = 0.0;
      configure(s);
      s.voice.start(pitchOf(s), gainOf(s));
   }

   // The same note if it is still sounding, else a free voice, else the quietest releasing one,
   // else the oldest.
   Slot &pickSlot(int16_t port, int16_t channel, int16_t key, bool panel) {
      for (Slot &s : slots_)
         if (s.voice.isActive() && s.key == key && s.channel == channel && s.port == port && s.panel == panel)
            return s;
      for (Slot &s : slots_)
         if (!s.voice.isActive())
            return s;
      Slot *best = nullptr;
      for (Slot &s : slots_)
         if (s.voice.isReleasing() && (!best || s.voice.level() < best->voice.level()))
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
      if (!out || s.panel)
         return;
      clap_event_note_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0};
      ev.note_id = s.noteId;
      ev.port_index = s.port;
      ev.channel = s.channel;
      ev.key = s.key;
      out->try_push(out, &ev.header);
   }

   // Copies the held notes where the panel can read them.
   void publishVoices() {
      // The LFO light follows the newest sounding note.
      const Slot *newest = nullptr;
      for (int i = 0; i < kVoiceCount; ++i) {
         const Slot &s = slots_[i];
         const bool held = s.voice.isActive() && !s.voice.isReleasing();
         if (s.voice.isActive() && (!newest || s.started > newest->started))
            newest = &s;
         shown_[i].note.store(held ? s.key : -1);
         shown_[i].bend.store(static_cast<float>(pitchOf(s) - s.key - std::round(value(kTune, &s))));
         shown_[i].pressure.store(static_cast<float>(pressureOf(s)));
         shown_[i].timbre.store(static_cast<float>(timbreOf(s)));
         for (int e = 0; e < kEnvCount; ++e) {
            // Idle (or a silent voice) maps to -1; attack, decay, sustain, release to 0..3.
            shown_[i].envStage[e].store(s.voice.isActive() ? static_cast<int>(s.voice.envStage(e)) - 1 : -1);
            shown_[i].envLevel[e].store(s.voice.envLevel(e));
            shown_[i].envFrom[e].store(s.voice.envReleaseFrom(e));
         }
      }
      for (int e = 0; e < kEnvCount; ++e) {
         loopShown_[e].stage.store(looping_[e] ? static_cast<int>(loopEnv_[e].stage()) - 1 : -1);
         loopShown_[e].level.store(loopEnv_[e].level());
      }
      lfoShown_.store(newest != nullptr);
      if (newest)
         lfoValue_.store(static_cast<float>(newest->voice.lfoValue()));
   }

   std::atomic<double> values_[kParamCount];
   // Global modulation from the host, written by the audio thread.
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;

   // Audio thread.
   Slot slots_[kVoiceCount];
   ChannelState channels_[16];
   jclap::dsp::Vca output_;
   uint64_t clock_ = 0;
   jclap::dsp::Envelope loopEnv_[kEnvCount];
   bool looping_[kEnvCount] = {};

   // Shared with the panel.
   struct Shown {
      std::atomic<int> note{-1};
      std::atomic<float> bend{0.0f}, pressure{0.0f}, timbre{0.5f};
      std::atomic<int> envStage[kEnvCount] = {-1, -1};
      std::atomic<float> envLevel[kEnvCount] = {}, envFrom[kEnvCount] = {};
   };
   Shown shown_[kVoiceCount];
   std::atomic<bool> lfoShown_{false};
   struct LoopShown {
      std::atomic<int> stage{-1};
      std::atomic<float> level{0.0f};
   };
   LoopShown loopShown_[kEnvCount];
   std::atomic<float> lfoValue_{0.0f};
   jclap::core::SpscQueue<PanelNote> panelNotes_;
   jclap::core::SampleFifo<4096> scope_;

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t BasicSynth::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.basic-synth",
   "Basic Synth",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.5.0",
   "An eight-voice MPE synth: oscillator, filter, VCA, two envelopes and an LFO",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(BasicSynth)
