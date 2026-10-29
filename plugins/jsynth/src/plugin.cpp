// JSynth: a bi-timbral MPE synth where every note is a creature in a flock. The engine (engine.h)
// makes the sound and flies the flock; this file is the CLAP side: params, state, notes and ports,
// and what the panel reads back.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/core/triple_buffer.h>
#include <jclap/dsp/denormals.h>
#include <jclap/ui/clap_gui.h>

#include <clap/ext/voice-info.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"

namespace {

using namespace jsynth;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

// A note from the panel to the audio thread.
struct PanelNote {
   enum class Kind : uint8_t { On, Expression, Off } kind;
   int8_t note;
   float velocity, bend, pressure, timbre;
};

// The notes held right now, as the panel sees them.
constexpr int kMaxShown = 32;
struct HeldNotes {
   EditorHost::Note notes[kMaxShown];
   int count = 0;
};

// State layout: 4-byte magic, uint32 version, uint32 count, then that many (uint32 id, double value)
// pairs. Ids have gaps and blocks grow, so each value carries its id; an id this build doesn't know
// is skipped, and a param the state doesn't mention keeps its default. Version 2 adds the preset's
// name after the values: a uint32 length, then that many bytes. Version 3 adds Release as a route
// source; an older state's route sources are moved to match. Version 4 adds the bus names after the
// preset's name, and version 5 the sky's look and each look's palette after them (three uint32s).
constexpr char kStateMagic[4] = {'J', 'J', 'S', 'Y'};
constexpr uint32_t kStateVersion = 6;

class JSynth final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JSynth(const clap_host_t *host)
       : Plugin(&descriptor, host), values_(new std::atomic<double>[paramCount()]),
         mods_(new std::atomic<double>[paramCount()]), pending_(new std::atomic<bool>[paramCount()]),
         engine_(new Engine) {
      for (size_t i = 0; i < paramCount(); ++i) {
         values_[i].store(kParams()[i].def.defaultValue);
         mods_[i].store(0.0);
         pending_[i].store(false);
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
      sampleRate_ = sampleRate;
      engine_->prepare(sampleRate, {values_.get(), mods_.get()});
      held_.clear();
      // Panel notes played while inactive are dropped. The audio thread isn't running, so popping
      // here is safe.
      PanelNote stale;
      while (panelNotes_.pop(stale)) {
      }
      publish();
      return true;
   }

   void reset() override { engine_->reset(1); }

   // An instrument: notes in, and three stereo outs: the main mix, then part A's and part B's own
   // (a part set to Output Own plays there instead of in the main mix).
   uint32_t audioPortsCount(bool isInput) const override { return isInput ? 0 : 3; }
   bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const override {
      if (isInput || index >= 3)
         return false;
      info->id = index;
      std::snprintf(info->name, sizeof(info->name), "%s", index == 0 ? "Main" : index == 1 ? "Part A" : "Part B");
      info->channel_count = 2;
      info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
      info->port_type = CLAP_PORT_STEREO;
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
      if (!std::strcmp(id, CLAP_EXT_VOICE_INFO))
         return &voiceInfo_;
      return jclap::ui::ClapGui::extensionFor<JSynth, &JSynth::gui_>(id);
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override {
      const int i = paramIndex(id);
      return i < 0 ? 0.0 : values_[i].load();
   }
   double paramModulated(clap_id id) const override {
      const int i = paramIndex(id);
      if (i < 0)
         return 0.0;
      const jclap::core::ParamDef &d = kParams()[i].def;
      return std::clamp(values_[i].load() + mods_[i].load(), d.min, d.max);
   }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double value) override {
      const int i = paramIndex(id);
      if (i < 0)
         return;
      const jclap::core::ParamDef &d = kParams()[i].def;
      value = std::clamp(value, d.min, d.max);
      // A stepped param (a choice, a key) only takes whole values, so the host records whole ones.
      if (d.flags & CLAP_PARAM_IS_STEPPED)
         value = std::round(value);
      values_[i].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   void setParams(const double *values) override {
      bool changed = false;
      for (size_t i = 0; i < paramCount(); ++i) {
         if (!std::isfinite(values[i]))
            continue;
         const jclap::core::ParamDef &d = kParams()[i].def;
         double v = std::clamp(values[i], d.min, d.max);
         if (d.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         if (values_[i].load() == v)
            continue;
         values_[i].store(v);
         pending_[i].store(true, std::memory_order_release);
         changed = true;
      }
      if (changed)
         if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
            hostParams->request_flush(host());
   }
   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }
   BusNames busNames() const override { return busNames_; }
   void setBusNames(const BusNames &names) override { busNames_ = names; }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   // While the host isn't processing, the panel's notes wait in the queue. Note-ons and expressions
   // stop going in well before it's full, so the note-offs that follow always fit and no note sticks.
   void noteOn(int note, float velocity) override {
      if (panelNotes_.size() < kPanelQueueRoom)
         panelNotes_.push({PanelNote::Kind::On, static_cast<int8_t>(note), velocity, 0, 0, 0.5f});
   }
   void noteExpression(int note, float bend, float pressure, float timbre) override {
      if (panelNotes_.size() < kPanelQueueRoom)
         panelNotes_.push({PanelNote::Kind::Expression, static_cast<int8_t>(note), 0, bend, pressure, timbre});
   }
   void noteOff(int note) override { panelNotes_.push({PanelNote::Kind::Off, static_cast<int8_t>(note), 0, 0, 0, 0}); }
   void notes(std::vector<Note> &out) const override {
      shown_.update();
      const HeldNotes &h = shown_.front();
      out.assign(h.notes, h.notes + h.count);
   }

   // ---- params ----

   uint32_t paramsCount() const override { return static_cast<uint32_t>(paramCount()); }
   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= paramCount())
         return false;
      jclap::core::fillParamInfo(kParams()[index].def, info);
      return true;
   }
   bool paramsValue(clap_id id, double *value) const override {
      const int i = paramIndex(id);
      if (i < 0)
         return false;
      *value = values_[i].load();
      return true;
   }
   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (!findParam(id))
         return false;
      formatParam(id, value, out, size);
      return true;
   }
   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      return parseParam(id, text, *value);
   }
   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleParam(in->get(in, i));
      drainEdits(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t count = static_cast<uint32_t>(paramCount());
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, &kStateVersion, 4) ||
          !jclap::core::writeAll(stream, &count, 4))
         return false;
      for (size_t i = 0; i < paramCount(); ++i) {
         const uint32_t id = kParams()[i].def.id;
         const double v = values_[i].load();
         if (!jclap::core::writeAll(stream, &id, 4) || !jclap::core::writeAll(stream, &v, sizeof(v)))
            return false;
      }
      const auto writeText = [stream](const std::string &text, size_t most) {
         const uint32_t length = static_cast<uint32_t>(std::min(text.size(), most));
         return jclap::core::writeAll(stream, &length, 4) && (length == 0 || jclap::core::writeAll(stream, text.data(), length));
      };
      if (!writeText(presetName_, 255))
         return false;
      // Version 4: the bus names, part A's then B's.
      for (const auto &part : busNames_)
         for (const std::string &name : part)
            if (!writeText(name, kBusNameMax))
               return false;
      // Version 5: how the sky is drawn.
      const uint32_t sky[3] = {static_cast<uint32_t>(skyLook), static_cast<uint32_t>(skyPalettes[0]),
                               static_cast<uint32_t>(skyPalettes[1])};
      if (!jclap::core::writeAll(stream, sky, sizeof(sky)))
         return false;
      // Version 6: how big the creatures are drawn.
      return jclap::core::writeAll(stream, &creatureScale, sizeof(creatureScale));
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t version = 0, count = 0;
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4) || version < 1 || version > kStateVersion ||
          !jclap::core::readAll(stream, &count, 4))
         return false;
      // Read everything before changing anything, so a bad state leaves the current one alone.
      std::vector<double> loaded(paramCount());
      for (size_t i = 0; i < paramCount(); ++i)
         loaded[i] = kParams()[i].def.defaultValue;
      for (uint32_t n = 0; n < count; ++n) {
         uint32_t id = 0;
         double v = 0.0;
         if (!jclap::core::readAll(stream, &id, 4) || !jclap::core::readAll(stream, &v, sizeof(v)) || !std::isfinite(v))
            return false;
         const int i = paramIndex(id);
         if (i >= 0)
            loaded[i] = std::clamp(version < 3 ? migrateBeforeRelease(id, v) : v, kParams()[i].def.min,
                                   kParams()[i].def.max);
      }
      const auto readText = [stream](std::string &text, uint32_t most) {
         uint32_t length = 0;
         if (!jclap::core::readAll(stream, &length, 4) || length > most)
            return false;
         text.resize(length);
         return length == 0 || jclap::core::readAll(stream, text.data(), length);
      };
      // The preset's name follows the values from version 2, and the bus names from version 4.
      std::string name;
      if (version >= 2 && !readText(name, 255))
         return false;
      BusNames buses{};
      if (version >= 4)
         for (auto &part : buses)
            for (std::string &bus : part)
               if (!readText(bus, kBusNameMax))
                  return false;
      uint32_t sky[3] = {static_cast<uint32_t>(skyLook), static_cast<uint32_t>(skyPalettes[0]),
                         static_cast<uint32_t>(skyPalettes[1])};
      if (version >= 5 && !jclap::core::readAll(stream, sky, sizeof(sky)))
         return false;
      float scale = creatureScale;
      if (version >= 6 && (!jclap::core::readAll(stream, &scale, sizeof(scale)) || !std::isfinite(scale)))
         return false;
      for (size_t i = 0; i < paramCount(); ++i)
         values_[i].store(loaded[i]);
      presetName_ = name;
      busNames_ = buses;
      // Out-of-range values are clamped by the panel when it draws.
      skyLook = static_cast<int>(std::min<uint32_t>(sky[0], 1));
      skyPalettes[0] = static_cast<int>(std::min<uint32_t>(sky[1], 255));
      skyPalettes[1] = static_cast<int>(std::min<uint32_t>(sky[2], 255));
      creatureScale = std::clamp(scale, kCreatureScaleMin, kCreatureScaleMax);
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      jclap::dsp::ScopedNoDenormals noDenormals;
      drainEdits(proc->out_events);
      if (const clap_event_transport_t *t = proc->transport) {
         const double beats = static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR;
         engine_->transport((t->flags & CLAP_TRANSPORT_HAS_TEMPO) ? t->tempo : 0.0, (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0,
                            (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) ? &beats : nullptr);
      }

      // Where the engine writes: the main port, then each part's own. A part whose port is missing
      // plays in the main mix.
      const int frames = static_cast<int>(proc->frames_count);
      float *channels[3][2] = {};
      float *const *ports[3] = {nullptr, nullptr, nullptr};
      for (uint32_t p = 0; p < proc->audio_outputs_count && p < 3; ++p) {
         const clap_audio_buffer_t &out = proc->audio_outputs[p];
         if (!out.data32 || out.channel_count < 2)
            continue;
         for (int c = 0; c < 2; ++c) {
            channels[p][c] = out.data32[c];
            std::fill(out.data32[c], out.data32[c] + frames, 0.0f);
         }
         ports[p] = channels[p];
         proc->audio_outputs[p].constant_mask = 0;
      }

      // The panel's notes start at the top of the block.
      engine_->setBlockSample(0);
      PanelNote p;
      while (panelNotes_.pop(p)) {
         NoteRef ref;
         ref.key = p.note;
         ref.port = -1;
         if (p.kind == PanelNote::Kind::On) {
            press(p.note, -1, p.velocity);
            engine_->noteOn(ref, p.velocity);
         }
         else if (p.kind == PanelNote::Kind::Off) {
            lift(p.note, -1, 0.0f);
            engine_->noteOff(ref, 0.0f);
         }
         else {
            if (HeldNote *h = find(p.note, -1)) {
               h->shown.bend = p.bend;
               h->shown.pressure = p.pressure;
               h->shown.timbre = p.timbre;
            }
            engine_->expression(ref, CLAP_NOTE_EXPRESSION_TUNING, p.bend);
            engine_->expression(ref, CLAP_NOTE_EXPRESSION_PRESSURE, p.pressure);
            engine_->expression(ref, CLAP_NOTE_EXPRESSION_BRIGHTNESS, p.timbre);
         }
      }

      // Render up to each event, then apply it: notes land on their sample, and params are read at
      // the next control tick.
      const uint32_t n = proc->in_events->size(proc->in_events);
      int at = 0;
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = proc->in_events->get(proc->in_events, i);
         const int time = std::clamp(static_cast<int>(h->time), at, frames);
         if (time > at) {
            renderSpan(ports, at, time - at);
            at = time;
         }
         engine_->setBlockSample(at);
         if (!handleParam(h))
            handleNote(h);
      }
      if (at < frames)
         renderSpan(ports, at, frames - at);

      // Notes that have finished sounding.
      for (int e = 0; e < engine_->endedCount(); ++e) {
         const Engine::Ended &end = engine_->ended(e);
         clap_event_note_t ev{};
         ev.header = {sizeof(ev), static_cast<uint32_t>(std::clamp(end.sample, 0, std::max(0, frames - 1))),
                      CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0};
         ev.note_id = end.ref.noteId;
         ev.port_index = end.ref.port;
         ev.channel = end.ref.channel;
         ev.key = end.ref.key;
         proc->out_events->try_push(proc->out_events, &ev.header);
      }
      engine_->clearEnded();

      publish();
      sinceFrame_ += frames;
      streamTime_ += frames / sampleRate_;
      if (sinceFrame_ >= static_cast<int>(sampleRate_ / 120.0)) {
         sinceFrame_ = 0;
         publishFlock();
      }
      return CLAP_PROCESS_CONTINUE;
   }

   // ---- what the panel reads (main thread) ----

   bool flockFrame(FlockFrame &out) const override {
      // The engine's flock, if it published one in the last quarter second.
      if (nowMs() - lastFrame_.load(std::memory_order_relaxed) > 250)
         return false;
      flockFrames_.update();
      out = flockFrames_.front();
      return true;
   }

private:
   // A held note, and the channel it came on (-1 for CLAP and panel notes, which get per-note
   // expressions). MPE MIDI's bend, pressure and CC 74 on that channel are its own.
   struct HeldNote {
      int channel;
      Note shown;
   };
   static constexpr size_t kPanelQueueRoom = 192;

   bool handleParam(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return false;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         const int i = paramIndex(ev->param_id);
         // Per-voice values (a note id, key or channel) have no voice to go to yet.
         if (i >= 0 && std::isfinite(ev->value) && ev->note_id == -1 && ev->key == -1 && ev->channel == -1)
            values_[i].store(std::clamp(ev->value, kParams()[i].def.min, kParams()[i].def.max));
         return true;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         const int i = paramIndex(ev->param_id);
         if (i < 0 || !std::isfinite(ev->amount))
            return true;
         if (ev->note_id == -1 && ev->key == -1 && ev->channel == -1)
            mods_[i].store(ev->amount);
         else
            engine_->voiceMod(hostRef(ev->note_id, ev->port_index, ev->channel, ev->key), i, static_cast<float>(ev->amount));
         return true;
      }
      return false;
   }

   void handleNote(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      switch (h->type) {
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF:
      case CLAP_EVENT_NOTE_CHOKE: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         const NoteRef ref = hostRef(ev->note_id, ev->port_index, ev->channel, ev->key);
         if (h->type == CLAP_EVENT_NOTE_ON) {
            press(ev->key, ev->channel, static_cast<float>(ev->velocity));
            engine_->noteOn(ref, static_cast<float>(ev->velocity));
         }
         else if (h->type == CLAP_EVENT_NOTE_OFF) {
            lift(ev->key, ev->channel, static_cast<float>(ev->velocity));
            engine_->noteOff(ref, static_cast<float>(ev->velocity));
         }
         else {
            held_.clear();
            engine_->choke(ref);
         }
         break;
      }
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         engine_->expression(hostRef(ev->note_id, ev->port_index, ev->channel, ev->key), ev->expression_id, ev->value);
         for (HeldNote &n : held_) {
            if ((ev->key != -1 && ev->key != n.shown.note) || (ev->channel != -1 && ev->channel != n.channel))
               continue;
            const float v = static_cast<float>(ev->value);
            if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
               n.shown.bend = v;
            else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
               n.shown.pressure = v;
            else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
               n.shown.timbre = v;
         }
         break;
      }
      case CLAP_EVENT_MIDI: {
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         engine_->midi(ev->data, ev->port_index);
         // The panel's copy: notes, and each member channel's own bend, pressure and CC 74.
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         if (status == 0x90 && ev->data[2] > 0)
            press(ev->data[1], channel, ev->data[2] / 127.0f);
         else if (status == 0x80 || status == 0x90)
            lift(ev->data[1], channel, ev->data[2] / 127.0f);
         for (HeldNote &n : held_) {
            if (n.channel != channel || channel == 0)
               continue;
            if (status == 0xE0)
               n.shown.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0f *
                              static_cast<float>(paramValue(partId(0, kBendRange)));
            else if (status == 0xD0)
               n.shown.pressure = ev->data[1] / 127.0f;
            else if (status == 0xB0 && ev->data[1] == 74)
               n.shown.timbre = ev->data[2] / 127.0f;
         }
         break;
      }
      default:
         break;
      }
   }

   static NoteRef hostRef(int32_t noteId, int16_t port, int16_t channel, int16_t key) {
      NoteRef ref;
      ref.noteId = noteId;
      ref.port = port;
      ref.channel = channel;
      ref.key = key;
      ref.host = true;
      return ref;
   }

   void renderSpan(float *const *const *ports, int at, int frames) {
      if (ports[0]) {
         engine_->render(ports, at, frames);
         return;
      }
      // No main port to write to: render into scratch so time (and the flock) keeps moving.
      while (frames > 0) {
         const int n = std::min(frames, kScratch);
         engine_->render(scratchPorts_, 0, n, at);
         at += n;
         frames -= n;
      }
   }

   static int64_t nowMs() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count();
   }

   HeldNote *find(int note, int channel) {
      for (HeldNote &n : held_)
         if (n.shown.note == note && n.channel == channel)
            return &n;
      return nullptr;
   }
   void press(int note, int channel, float velocity) {
      if (note < 0 || note > 127)
         return;
      if (HeldNote *n = find(note, channel)) {
         n->shown.velocity = velocity;
         return;
      }
      if (held_.size() >= kMaxShown)
         held_.erase(held_.begin());
      held_.push_back({channel, {note, 0.0f, 0.0f, 0.5f, velocity, 0.0f}});
   }
   void lift(int note, int channel, float) {
      held_.erase(std::remove_if(held_.begin(), held_.end(),
                                 [&](const HeldNote &n) { return n.shown.note == note && n.channel == channel; }),
                  held_.end());
   }

   // Tells the host about the panel's edits: knob gestures in order, then each param a preset load
   // changed, as one edit carrying its latest value.
   void drainEdits(const clap_output_events_t *out) {
      edits_.drain(out);
      for (size_t i = 0; i < paramCount(); ++i)
         if (pending_[i].load(std::memory_order_relaxed) && pending_[i].exchange(false, std::memory_order_acquire)) {
            const clap_id id = kParams()[i].def.id;
            jclap::core::pushParamGesture(out, id, true);
            jclap::core::pushParamValue(out, id, values_[i].load());
            jclap::core::pushParamGesture(out, id, false);
         }
   }

   void publish() {
      HeldNotes &h = shown_.back();
      h.count = 0;
      for (const HeldNote &n : held_)
         h.notes[h.count++] = n.shown;
      shown_.publish();
   }

   void publishFlock() {
      FlockFrame &f = flockFrames_.back();
      engine_->flock().show(f);
      engine_->showEnvelopes(f);
      for (int part = 0; part < kPartCount; ++part) {
         for (int e = 0; e < kEnvCount; ++e)
            f.env[part][e] = engine_->envLevel(part, e);
         for (int l = 0; l < kLfoCount; ++l)
            f.lfo[part][l] = engine_->lfoLevel(part, l);
         for (int b = 0; b < kBusCount; ++b)
            f.bus[part][b] = engine_->busLevel(part, b);
      }
      for (int i = 0; i < 3; ++i)
         f.meter[i] = engine_->takeMeter(i);
      for (int part = 0; part < kPartCount; ++part)
         for (int k = 0; k < kFilterCount; ++k)
            if (!engine_->filterNow(part, k, f.cutoff[part][k], f.resonance[part][k]))
               f.cutoff[part][k] = 0.0f;
      for (int part = 0; part < kPartCount; ++part)
         if (!engine_->destinationsNow(part, f.dest[part], f.moved[part]))
            std::fill(std::begin(f.moved[part]), std::end(f.moved[part]), false);
      f.time = streamTime_;
      flockFrames_.publish();
      lastFrame_.store(nowMs(), std::memory_order_relaxed);
   }

   static bool getVoiceInfo(const clap_plugin_t *, clap_voice_info_t *info) {
      info->voice_count = 2 * kMaxPolyphony;
      info->voice_capacity = kVoiceSlots;
      info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
      return true;
   }
   static constexpr clap_plugin_voice_info_t voiceInfo_ = {getVoiceInfo};

   std::unique_ptr<std::atomic<double>[]> values_, mods_;
   // Params a preset load changed that the host hasn't been told about yet.
   std::unique_ptr<std::atomic<bool>[]> pending_;
   jclap::core::EditQueue edits_;
   // Main thread.
   std::string presetName_ = "Init";
   BusNames busNames_{};
   jclap::ui::HostMenu hostMenu_;
   jclap::core::SpscQueue<PanelNote> panelNotes_;
   // Audio thread only. Reserved in the constructor's place by the cap, so it never allocates past it.
   std::vector<HeldNote> held_ = [] {
      std::vector<HeldNote> v;
      v.reserve(kMaxShown);
      return v;
   }();
   mutable jclap::core::TripleBuffer<HeldNotes> shown_;
   std::unique_ptr<Engine> engine_;
   double sampleRate_ = 48000.0;
   int sinceFrame_ = 0;
   // Seconds of audio processed, for the flock frames' times.
   double streamTime_ = 0.0;
   mutable jclap::core::TripleBuffer<FlockFrame> flockFrames_;
   std::atomic<int64_t> lastFrame_{0};
   // Somewhere to render when the host gives no main port.
   static constexpr int kScratch = 256;
   std::vector<float> scratch_ = std::vector<float>(2 * kScratch);
   float *scratchChannels_[2] = {scratch_.data(), scratch_.data() + kScratch};
   float *const *scratchPorts_[3] = {scratchChannels_, nullptr, nullptr};
   // Last, so the window closes before anything it reads is destroyed.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JSynth::descriptor = {
    CLAP_VERSION_INIT,
    "com.jeffmhopkins.jclap.jsynth",
    "JSynth",
    "jeffmhopkins",
    "https://github.com/jeffmhopkins/Jclap",
    "",
    "",
    JCLAP_PLUGIN_VERSION,
    "Bi-timbral MPE flocking FM synth",
    kFeatures,
};

} // namespace

JCLAP_ENTRY(JSynth)
