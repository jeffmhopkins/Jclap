// JArp: a CLAP note effect that arpeggiates held notes through a grid pattern, in time with the
// host, keeping each finger's MPE. The engine (engine.h) plays the pattern; this file wires it to the
// host: params, state, the transport, notes and expression in and out, and the panel (editor.cpp).

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
#include "presets.h"

namespace {

using namespace jarp;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// State layout: 4-byte magic, uint32 version, uint32 param count, one double per param in kParams
// order, uint32 pattern size and the pattern (Pattern::save), then uint32 name length and the preset
// name. Params are only ever appended, so a state with fewer values leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', 'A', 'R', 'P'};
constexpr uint32_t kStateVersion = 1;

class JArp final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JArp(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
      pattern_ = defaultPattern();
      engine_.setPattern(pattern_);
      engine_.setSettings(settings());
      publish();
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
      // Patterns sent while inactive were applied at once, so the queue only holds stale ones.
      Command stale;
      while (commands_.pop(stale)) {
      }
      engine_.setPattern(pattern_);
      engine_.setSettings(settings());
      engine_.reset();
      active_.store(true);
      return true;
   }

   void deactivate() override {
      active_.store(false);
      engine_.reset();
      publish();
   }

   void reset() override { engine_.reset(); }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<JArp, &JArp::gui_>(id);
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes In" : "Arp Out");
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

   const Frame &frame() override {
      // While inactive nothing else runs the engine, so the panel keeps it in step with the params.
      if (!active_.load()) {
         engine_.setSettings(settings());
         publish();
      }
      frames_.update();
      return frames_.front();
   }

   const Pattern &pattern() const override { return pattern_; }

   void setPattern(const Pattern &p) override {
      if (p == pattern_)
         return;
      pattern_ = p;
      if (active_.load())
         commands_.push({p});
      else
         engine_.setPattern(p);
      // The pattern isn't a param, so the host is told the project changed.
      if (auto *state = hostExtension<clap_host_state_t>(CLAP_EXT_STATE))
         state->mark_dirty(host());
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
      const std::vector<uint8_t> saved = pattern_.save();
      const uint32_t size = static_cast<uint32_t>(saved.size());
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &size, sizeof(size)) && jclap::core::writeAll(stream, saved.data(), size) &&
             jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t header[2] = {};
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, header, sizeof(header)) || header[0] < 1 || header[0] > kStateVersion ||
          header[1] > 4096)
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
      uint32_t size = 0;
      if (!jclap::core::readAll(stream, &size, sizeof(size)) || size > 65536)
         return false;
      std::vector<uint8_t> saved(size);
      Pattern p;
      if (!jclap::core::readAll(stream, saved.data(), size) || !p.load(saved.data(), saved.size()))
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
      pattern_ = p;
      if (active_.load())
         commands_.push({p});
      else
         engine_.setPattern(p);
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      edits_.drain(out);

      // Params apply at the start of the block, before anything plays.
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleParam(in->get(in, i));
      Command c;
      while (commands_.pop(c))
         engine_.setPattern(c.pattern);
      engine_.setSettings(settings());

      Block b;
      b.frames = proc->frames_count;
      b.sampleRate = sampleRate_;
      if (const clap_event_transport_t *t = proc->transport) {
         if (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
            b.tempo = t->tempo;
         if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
            b.hasTransport = true;
            b.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
            b.beats = static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR;
            b.hasBar = true;
            b.barStart = static_cast<double>(t->bar_start) / CLAP_BEATTIME_FACTOR;
         }
      }
      out_.clear();
      engine_.beginBlock(b, out_);
      flush(out);

      // Notes and expression in time order, the arp's notes sent up to each one first.
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         handleEvent(h, out);
      }
      engine_.advance(proc->frames_count, out_);
      flush(out);

      publish();
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   void flush(const clap_output_events_t *out) {
      for (size_t i = 0; i < out_.count; ++i) {
         const MidiOut &m = out_.items[i];
         jclap::core::pushMidi(out, m.time, m.status, m.data1, m.data2);
      }
      out_.clear();
   }

   void handleEvent(const clap_event_header_t *h, const clap_output_events_t *out) {
      const uint32_t t = h->time;
      switch (h->type) {
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         if (ev->key < 0 || ev->key > 127)
            return;
         const int channel = ev->channel < 0 ? 0 : ev->channel & 15;
         if (h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0)
            engine_.noteOn(t, channel, ev->key, std::clamp(static_cast<int>(std::lround(ev->velocity * 127.0)), 1, 127), out_);
         else
            engine_.noteOff(t, channel, ev->key, out_);
         flush(out);
         return;
      }
      case CLAP_EVENT_NOTE_EXPRESSION: {
         auto *ev = reinterpret_cast<const clap_event_note_expression_t *>(h);
         engine_.advance(t, out_);
         flush(out);
         const int channel = ev->channel < 0 ? -1 : ev->channel & 15;
         const int key = ev->key < 0 || ev->key > 127 ? -1 : ev->key;
         if (channel < 0) {
            out->try_push(out, h);
            return;
         }
         if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
            engine_.setPressure(channel, key, std::clamp(ev->value, 0.0, 1.0));
         else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
            engine_.setSlide(channel, key, std::clamp(ev->value, 0.0, 1.0));
         else if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
            engine_.setBend(channel, key, std::clamp(ev->value / 48.0, -1.0, 1.0));
         if (key < 0) {
            out->try_push(out, h);
            return;
         }
         // Re-keyed to each note sounding from that finger.
         engine_.forEachSounding(channel, key, [&](int k) {
            clap_event_note_expression_t copy = *ev;
            copy.key = static_cast<int16_t>(k);
            copy.note_id = -1;
            out->try_push(out, &copy.header);
         });
         return;
      }
      case CLAP_EVENT_MIDI: {
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         const int d1 = ev->data[1] & 0x7F, d2 = ev->data[2] & 0x7F;
         if (status == 0x90 || status == 0x80) {
            if (status == 0x90 && d2 > 0)
               engine_.noteOn(t, channel, d1, d2, out_);
            else
               engine_.noteOff(t, channel, d1, out_);
            flush(out);
            return;
         }
         engine_.advance(t, out_);
         flush(out);
         if (status == 0xA0) {
            engine_.setPressure(channel, d1, d2 / 127.0);
            engine_.forEachSounding(channel, d1, [&](int k) {
               jclap::core::pushMidi(out, t, ev->data[0], static_cast<uint8_t>(k), static_cast<uint8_t>(d2));
            });
            return;
         }
         if (status == 0xD0)
            engine_.setPressure(channel, -1, d1 / 127.0);
         else if (status == 0xB0 && d1 == 74)
            engine_.setSlide(channel, -1, d2 / 127.0);
         else if (status == 0xE0)
            engine_.setBend(channel, -1, ((d2 << 7 | d1) - 8192) / 8192.0);
         out->try_push(out, h);
         return;
      }
      default:
         return;
      }
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

   // Copies what the panel needs out of the engine. Called by whichever thread runs it.
   void publish() {
      engine_.fillFrame(frames_.back());
      frames_.publish();
   }

   Settings settings() const {
      const auto whole = [this](clap_id id) { return static_cast<int>(std::lround(value(id))); };
      Settings s;
      s.rate = whole(kRate);
      s.swing = value(kSwing);
      s.length = whole(kLength);
      s.latch = whole(kLatch) != 0;
      s.restart = static_cast<Restart>(whole(kRestart));
      s.degree = whole(kRowsBy) != 0;
      s.wrap = whole(kEmptyRows) != 0;
      s.key = whole(kKey);
      s.scale = whole(kScale);
      s.pressVelocity = value(kPressVelocity);
      s.curve = value(kCurve);
      s.pressRatchet = value(kPressRatchet);
      s.maxHits = whole(kMaxHits);
      return s;
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
   OutBuffer out_;
   jclap::core::SpscQueue<Command, 32> commands_;
   jclap::core::TripleBuffer<Frame> frames_;

   // Main thread.
   Pattern pattern_;
   std::string presetName_ = "Init";

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JArp::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jarp",
   "JArp",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "An MPE arpeggiator that plays held notes through a grid pattern",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JArp)
