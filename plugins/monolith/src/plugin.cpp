// Monolith: a CLAP note effect that reads a slab of generated glyphs as MIDI notes, in time with the
// host. The engine (engine.h) does the reading; this file wires it to the host: params, state, the
// transport, notes in and out, and the panel (editor.cpp).

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/core/triple_buffer.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"

namespace {

using namespace monolith;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// State layout: 4-byte magic, uint32 version, uint32 param count, one double per param in kParams
// order, uint32 slab size and the slab (Engine::save), then uint32 name length and the preset name.
// Params are only ever appended, so a state with fewer values leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', 'M', 'O', 'N'};
// Version 1 drew glyphs on a 3 x 4 grid and had ROWS where SPREAD is; it still loads.
constexpr uint32_t kStateVersion = 2;

// Notes the engine sends in one block, without allocating on the audio thread.
struct NoteBuffer {
   static constexpr size_t kCapacity = 512;
   NoteOut items[kCapacity];
   size_t count = 0;

   size_t size() const { return count; }
   void push_back(const NoteOut &n) {
      if (count < kCapacity)
         items[count++] = n;
   }
   NoteOut *begin() { return items; }
   NoteOut *end() { return items + count; }
   void clear() { count = 0; }
};

class Monolith final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit Monolith(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
      engine_.setParams(engineParams());
      publish();
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      // Commands sent while inactive were applied at once, so the queue only holds stale ones.
      Command stale;
      while (commands_.pop(stale))
         apply(stale);
      engine_.setParams(engineParams());
      engine_.resetVoice();
      active_.store(true);
      return true;
   }

   void deactivate() override {
      active_.store(false);
      engine_.resetVoice();
      publish();
   }

   void reset() override { engine_.resetVoice(); }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<Monolith, &Monolith::gui_>(id);
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes In" : "Glyph Notes");
      return true;
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double v) override {
      if (id >= kParamCount || !std::isfinite(v))
         return;
      v = std::clamp(v, kParams[id].min, kParams[id].max);
      values_[id].store(v);
      edits_.value(id, v);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }

   const SlabFrame &frame() override {
      // While inactive nothing else runs the engine, so the panel keeps it in step with the params.
      if (!active_.load()) {
         engine_.setParams(engineParams());
         publish();
      }
      frames_.update();
      return frames_.front();
   }

   void command(const Command &c) override {
      if (active_.load())
         commands_.push(c);
      else {
         apply(c);
         publish();
      }
   }

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

   bool paramsValue(clap_id id, double *v) const override {
      if (id >= kParamCount)
         return false;
      *v = values_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double v, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, v, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *v) const override {
      if (id >= kParamCount || !parseParam(id, text, *v))
         return false;
      *v = std::clamp(*v, kParams[id].min, kParams[id].max);
      return true;
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleParam(in->get(in, i));
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
      const std::vector<uint8_t> slab = currentSlab();
      const uint32_t slabSize = static_cast<uint32_t>(slab.size());
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &slabSize, sizeof(slabSize)) &&
             jclap::core::writeAll(stream, slab.data(), slabSize) &&
             jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t header[2] = {};
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, header, sizeof(header)) || header[0] < 1 || header[0] > kStateVersion || header[1] > 4096)
         return false;
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
      uint32_t slabSize = 0;
      if (!jclap::core::readAll(stream, &slabSize, sizeof(slabSize)) || slabSize > 65536)
         return false;
      std::vector<uint8_t> slab(slabSize);
      if (!jclap::core::readAll(stream, slab.data(), slabSize))
         return false;
      if (header[0] == 1) {
         if (!Engine::upgradeFromFourRows(slab))
            return false;
         // Wide, Stacked, Steps, Open become the nearest spread.
         static constexpr double kSpreadOf[4] = {1, 1, 0, 3};
         v[kSpread] = kSpreadOf[std::clamp(static_cast<int>(std::lround(v[kSpread])), 0, 3)];
      }
      std::vector<Command> loads;
      if (!Engine::load(slab.data(), slab.size(), [&](const Command &c) { loads.push_back(c); }))
         return false;
      uint32_t nameLength = 0;
      char name[256];
      if (jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) && nameLength < sizeof(name) &&
          jclap::core::readAll(stream, name, nameLength))
         presetName_.assign(name, nameLength);
      else
         presetName_.clear();

      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], kParams[i].min, kParams[i].max));
      // Until the audio thread has put the slab in place, a save returns it as loaded.
      loadedSlab_ = slab;
      loadsRequested_.fetch_add(1);
      for (const Command &c : loads)
         command(c);
      if (!active_.load())
         loadsApplied_.store(loadsRequested_.load());
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      edits_.drain(out);

      // Params apply at the start of the block; notes are collected with their times.
      NoteIn notes[256];
      int noteCount = 0;
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (!handleParam(h) && noteCount < 256)
            readNote(h, notes, noteCount);
      }

      bool loaded = false;
      Command c;
      while (commands_.pop(c)) {
         apply(c);
         loaded = loaded || c.kind == Command::Kind::LoadKey;
      }
      engine_.setParams(engineParams());

      Block b;
      b.frames = proc->frames_count;
      b.sampleRate = sampleRate_;
      if (const clap_event_transport_t *t = proc->transport; t && (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)) {
         b.hasTransport = true;
         b.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
         b.tempo = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) ? t->tempo : 120.0;
         b.beats = static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR;
      }
      generated_.clear();
      engine_.process(b, notes, noteCount, generated_);

      // Played notes pass through, except while key follow is using them; they merge with the
      // glyph notes in time order.
      const EngineParams &p = engine_.params();
      const bool passThrough = p.mode == Mode::Listen || !p.follow;
      const uint8_t channel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value(kChannel))), 1, 16) - 1);
      size_t g = 0;
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (!passThrough || !jclap::core::isNoteEvent(h))
            continue;
         for (; g < generated_.size() && generated_.items[g].time <= h->time; ++g)
            send(out, generated_.items[g], channel);
         out->try_push(out, h);
      }
      for (; g < generated_.size(); ++g)
         send(out, generated_.items[g], channel);

      publish();
      if (loaded)
         loadsApplied_.fetch_add(1);
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   static void send(const clap_output_events_t *out, const NoteOut &note, uint8_t channel) {
      jclap::core::pushMidi(out, note.time, static_cast<uint8_t>((note.on ? 0x90 : 0x80) | channel), note.pitch,
                            note.on ? note.velocity : 0);
   }

   // A param value or modulation; false for any other event.
   bool handleParam(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return false;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->value))
            values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
         return true;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->amount))
            mods_[ev->param_id].store(ev->amount);
         return true;
      }
      return false;
   }

   static void readNote(const clap_event_header_t *h, NoteIn *notes, int &count) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         if (ev->key < 0 || ev->key > 127)
            return;
         const bool on = h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0;
         const int velocity = std::clamp(static_cast<int>(std::lround(ev->velocity * 127.0)), 1, 127);
         notes[count++] = {h->time, static_cast<uint8_t>(ev->key), static_cast<uint8_t>(velocity), on};
      }
      else if (h->type == CLAP_EVENT_MIDI) {
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0;
         if (status == 0x90 || status == 0x80)
            notes[count++] = {h->time, static_cast<uint8_t>(ev->data[1] & 0x7F), static_cast<uint8_t>(ev->data[2] & 0x7F),
                              status == 0x90 && ev->data[2] > 0};
      }
   }

   void apply(const Command &c) { engine_.apply(c); }

   // Copies what the panel and a save need out of the engine. Called by whichever thread runs it.
   void publish() {
      engine_.fillFrame(frames_.back());
      frames_.publish();
   }

   // The slab as a save should hold it.
   std::vector<uint8_t> currentSlab() {
      if (loadsApplied_.load() < loadsRequested_.load())
         return loadedSlab_;
      const SlabFrame &f = frame();
      return Engine::save(f.cells, f.key);
   }

   EngineParams engineParams() const {
      const auto whole = [this](clap_id id) { return static_cast<int>(std::lround(value(id))); };
      EngineParams p;
      p.orthography = static_cast<Orthography>(whole(kOrthography));
      p.seed = whole(kSeed);
      p.lexicon = whole(kLexicon);
      p.density = whole(kDensity);
      p.grammar = value(kGrammar);
      p.drift = value(kDrift);
      p.erode = value(kErode);
      p.lock = value(kLock) >= 0.5;
      p.length = whole(kLength);
      p.rate = whole(kRate);
      p.direction = static_cast<Direction>(whole(kDirection));
      p.mode = static_cast<Mode>(whole(kMode));
      p.swing = value(kSwing);
      p.root = whole(kRoot);
      p.scale = whole(kScale);
      p.spread = whole(kSpread);
      p.octave = whole(kOctave);
      p.velocity = value(kVelocity);
      p.gate = value(kGate);
      p.humanize = value(kHumanize);
      p.follow = value(kFollow) >= 0.5;
      return p;
   }

   // A param as the engine hears it: its value plus host modulation, clamped to its range.
   double value(clap_id id) const {
      return std::clamp(values_[id].load() + mods_[id].load(), kParams[id].min, kParams[id].max);
   }

   std::atomic<double> values_[kParamCount];
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;

   // The engine belongs to the audio thread while active, and to the main thread otherwise.
   Engine engine_;
   double sampleRate_ = 48000.0;
   std::atomic<bool> active_{false};
   NoteBuffer generated_;
   jclap::core::SpscQueue<Command, 256> commands_;
   jclap::core::TripleBuffer<SlabFrame> frames_;

   // Main thread.
   std::string presetName_ = "Init";
   std::vector<uint8_t> loadedSlab_;
   std::atomic<uint32_t> loadsRequested_{0}, loadsApplied_{0};

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t Monolith::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.monolith",
   "Monolith",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.1.0",
   "Reads a slab of generated alien glyphs as MIDI note phrases",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(Monolith)
