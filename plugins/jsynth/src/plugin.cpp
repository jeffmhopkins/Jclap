// JSynth: a bi-timbral MPE synth, so far only its panel and params. This is the mock: every param
// exists, saves and loads, and the panel edits it, but the plugin makes no sound yet. Notes from the
// host and the panel are tracked so the keyboard, note grid and touch lights show them.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/core/triple_buffer.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "editor.h"
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
// is skipped, and a param the state doesn't mention keeps its default.
constexpr char kStateMagic[4] = {'J', 'J', 'S', 'Y'};
constexpr uint32_t kStateVersion = 1;

class JSynth final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JSynth(const clap_host_t *host)
       : Plugin(&descriptor, host), values_(new std::atomic<double>[paramCount()]),
         mods_(new std::atomic<double>[paramCount()]) {
      for (size_t i = 0; i < paramCount(); ++i) {
         values_[i].store(kParams()[i].def.defaultValue);
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

   bool activate(double, uint32_t, uint32_t) override {
      held_.clear();
      // Panel notes played while inactive are dropped. The audio thread isn't running, so popping
      // here is safe.
      PanelNote stale;
      while (panelNotes_.pop(stale)) {
      }
      publish();
      return true;
   }

   // An instrument: notes in, stereo out.
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
      edits_.drain(out);
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
      return true;
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t version = 0, count = 0;
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4) || version != kStateVersion ||
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
            loaded[i] = std::clamp(v, kParams()[i].def.min, kParams()[i].def.max);
      }
      for (size_t i = 0; i < paramCount(); ++i)
         values_[i].store(loaded[i]);
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      edits_.drain(proc->out_events);
      const uint32_t n = proc->in_events->size(proc->in_events);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = proc->in_events->get(proc->in_events, i);
         if (!handleParam(h))
            handleNote(h);
      }
      PanelNote p;
      while (panelNotes_.pop(p)) {
         if (p.kind == PanelNote::Kind::On)
            press(p.note, -1, p.velocity);
         else if (p.kind == PanelNote::Kind::Off)
            lift(p.note, -1, 0.0f);
         else if (HeldNote *h = find(p.note, -1)) {
            h->shown.bend = p.bend;
            h->shown.pressure = p.pressure;
            h->shown.timbre = p.timbre;
         }
      }
      // No sound yet.
      for (uint32_t port = 0; port < proc->audio_outputs_count; ++port) {
         const clap_audio_buffer_t &out = proc->audio_outputs[port];
         for (uint32_t c = 0; c < out.channel_count; ++c) {
            if (out.data32)
               std::fill(out.data32[c], out.data32[c] + proc->frames_count, 0.0f);
            if (out.data64)
               std::fill(out.data64[c], out.data64[c] + proc->frames_count, 0.0);
         }
         proc->audio_outputs[port].constant_mask = ~uint64_t{0};
      }
      publish();
      return CLAP_PROCESS_SLEEP;
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
         if (i >= 0 && std::isfinite(ev->amount) && ev->note_id == -1 && ev->key == -1 && ev->channel == -1)
            mods_[i].store(ev->amount);
         return true;
      }
      return false;
   }

   void handleNote(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
      switch (h->type) {
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         if (h->type == CLAP_EVENT_NOTE_ON)
            press(ev->key, -1, static_cast<float>(ev->velocity));
         else
            lift(ev->key, -1, static_cast<float>(ev->velocity));
         break;
      }
      case CLAP_EVENT_NOTE_CHOKE:
         held_.clear();
         break;
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         for (HeldNote &n : held_) {
            if (ev->key != -1 && ev->key != n.shown.note)
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
         // MPE: each note has its own channel, so its bend, channel pressure and CC 74 are its own.
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         if (status == 0x90 && ev->data[2] > 0)
            press(ev->data[1], channel, ev->data[2] / 127.0f);
         else if (status == 0x80 || status == 0x90)
            lift(ev->data[1], channel, ev->data[2] / 127.0f);
         for (HeldNote &n : held_) {
            if (n.channel != channel)
               continue;
            if (status == 0xE0) // MPE's default 48 semitones on a note channel
               n.shown.bend = ((ev->data[2] << 7 | ev->data[1]) - 8192) / 8192.0f * 48.0f;
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
         n->shown.strike = velocity;
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

   void publish() {
      HeldNotes &h = shown_.back();
      h.count = 0;
      for (const HeldNote &n : held_)
         h.notes[h.count++] = n.shown;
      shown_.publish();
   }

   std::unique_ptr<std::atomic<double>[]> values_, mods_;
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   jclap::core::SpscQueue<PanelNote> panelNotes_;
   // Audio thread only. Reserved in the constructor's place by the cap, so it never allocates past it.
   std::vector<HeldNote> held_ = [] {
      std::vector<HeldNote> v;
      v.reserve(kMaxShown);
      return v;
   }();
   mutable jclap::core::TripleBuffer<HeldNotes> shown_;
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
    "0.7.0",
    "Bi-timbral MPE synth (panel mock, no sound yet)",
    kFeatures,
};

} // namespace

JCLAP_ENTRY(JSynth)
