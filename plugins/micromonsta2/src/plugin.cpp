// JClap MicroMonsta 2: a CLAP note effect that exposes every MicroMonsta 2 sound parameter as a host
// parameter and sends it out as a MIDI CC whenever it changes. Includes a knob panel (editor.cpp).

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cc_map.h"
#include "editor.h"

namespace {

using namespace mm2;

const char *sectionName(Section s) {
   switch (s) {
   case Section::Oscillators: return "Oscillators";
   case Section::Mix: return "Mix";
   case Section::Filter: return "Filter";
   case Section::Envelopes: return "Envelopes";
   case Section::Lfo: return "LFO";
   case Section::ModMatrix: return "Mod Matrix";
   case Section::Effects: return "Effects";
   case Section::Voice: return "Voice";
   case Section::Performance: return "Performance";
   }
   return "";
}

// State layout: 4-byte magic, uint32 version, int32 channel, uint32 count, then per CC parameter
// {int32 cc, int32 touched, double value}. Keyed by CC so the list can change between versions.
constexpr char kStateMagic[4] = {'J', 'M', 'M', '2'};
constexpr uint32_t kStateVersion = 1;

struct StateHeader {
   char magic[4];
   uint32_t version;
   int32_t channel;
   uint32_t count;
};

struct StateEntry {
   int32_t cc;
   int32_t touched;
   double value;
};

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

class MicroMonsta final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit MicroMonsta(const clap_host_t *host) : Plugin(&descriptor, host) {}

   // ---- plugin ----

   bool init() override {
      hostParams_ = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS);
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      resetDefaults();
      return true;
   }

   bool activate(double, uint32_t, uint32_t) override {
      // Resend everything that has been set, so a reopened project restores the synth's sound.
      resendRequested_.store(true);
      return true;
   }

   void reset() override {
      for (auto &m : mods_)
         m.store(0.0);
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
      return jclap::ui::ClapGui::extensionFor<MicroMonsta, &MicroMonsta::gui_>(id);
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes In" : "To MicroMonsta");
      return true;
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override {
      if (id == kChannelParamId)
         return midiChannel_.load();
      const int i = indexOfCc(static_cast<int>(id));
      return i >= 0 ? values_[i].load() : 0.0;
   }

   double paramModulated(clap_id id) const override {
      const int i = indexOfCc(static_cast<int>(id));
      return i >= 0 ? effective(i) : paramValue(id);
   }

   void beginEdit(clap_id id) override { edits_.begin(id); }

   void setParam(clap_id id, double value) override {
      if (id == kChannelParamId) {
         const int ch = std::clamp(static_cast<int>(std::lround(value)), 1, 16);
         midiChannel_.store(ch);
         edits_.value(id, ch);
         return;
      }
      const int i = indexOfCc(static_cast<int>(id));
      if (i < 0)
         return;
      value = std::clamp(value, 0.0, 127.0);
      values_[i].store(value);
      touched_[i].store(true);
      edits_.value(id, value);
   }

   void endEdit(clap_id id) override { edits_.end(id); }

   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }

   int channel() const override { return midiChannel_.load(); }

   void setChannel(int ch) override {
      beginEdit(kChannelParamId);
      setParam(kChannelParamId, ch);
      endEdit(kChannelParamId);
   }

   void sendAll() override {
      for (auto &t : touched_)
         t.store(true);
      resendRequested_.store(true);
      if (hostParams_)
         hostParams_->request_flush(host());
   }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= static_cast<uint32_t>(kParamCount))
         return false;
      if (index == static_cast<uint32_t>(kCcParamCount)) {
         jclap::core::fillParamInfo({kChannelParamId, "MIDI Channel", "Settings", 1, 16, 1, CLAP_PARAM_IS_STEPPED},
                                    info);
         return true;
      }
      const CcParam &p = kCcParams[index];
      jclap::core::fillParamInfo({static_cast<clap_id>(p.cc), p.name, sectionName(p.section), 0, 127,
                                  defaultValue(p), CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE},
                                 info);
      return true;
   }

   bool paramsValue(clap_id id, double *value) const override {
      if (id != kChannelParamId && indexOfCc(static_cast<int>(id)) < 0)
         return false;
      *value = paramValue(id);
      return true;
   }

   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id == kChannelParamId) {
         std::snprintf(out, size, "Ch %d", static_cast<int>(std::lround(value)));
         return true;
      }
      if (indexOfCc(static_cast<int>(id)) < 0)
         return false;
      std::snprintf(out, size, "%d", toCcValue(value));
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      if (id != kChannelParamId && indexOfCc(static_cast<int>(id)) < 0)
         return false;
      const char *s = text;
      if (std::strncmp(s, "Ch", 2) == 0)
         s += 2;
      double v;
      if (std::sscanf(s, "%lf", &v) != 1)
         return false;
      *value = v;
      return true;
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         applyParamEvent(in->get(in, i));
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      StateHeader header{};
      std::memcpy(header.magic, kStateMagic, sizeof(kStateMagic));
      header.version = kStateVersion;
      header.channel = midiChannel_.load();
      header.count = kCcParamCount;
      if (!jclap::core::writeAll(stream, &header, sizeof(header)))
         return false;
      for (int i = 0; i < kCcParamCount; ++i) {
         const StateEntry e{kCcParams[i].cc, touched_[i].load() ? 1 : 0, values_[i].load()};
         if (!jclap::core::writeAll(stream, &e, sizeof(e)))
            return false;
      }
      return true;
   }

   bool stateLoad(const clap_istream_t *stream) override {
      StateHeader header{};
      if (!jclap::core::readAll(stream, &header, sizeof(header)))
         return false;
      if (std::memcmp(header.magic, kStateMagic, sizeof(kStateMagic)) || header.version != kStateVersion ||
          header.count > 128)
         return false;

      StateEntry entries[128];
      for (uint32_t i = 0; i < header.count; ++i) {
         if (!jclap::core::readAll(stream, &entries[i], sizeof(StateEntry)) || !std::isfinite(entries[i].value))
            return false;
      }

      midiChannel_.store(std::clamp(static_cast<int>(header.channel), 1, 16));
      for (uint32_t e = 0; e < header.count; ++e) {
         const int i = indexOfCc(entries[e].cc);
         if (i < 0)
            continue;
         values_[i].store(std::clamp(entries[e].value, 0.0, 127.0));
         touched_[i].store(entries[e].touched != 0);
      }
      // Send the restored sound to the synth on the next block.
      resendRequested_.store(true);

      // Tell the host the values changed so it refreshes what it shows.
      if (hostParams_)
         hostParams_->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;

      edits_.drain(out);

      // Panel edits, state loads and channel changes land between blocks; send them at the start.
      const int ch = midiChannel_.load();
      if (ch != sentChannel_ || resendRequested_.exchange(false)) {
         sentChannel_ = ch;
         sender_.resendAll();
      }
      for (int i = 0; i < kCcParamCount; ++i)
         sendIfChanged(out, 0, i);

      const uint32_t n = in->size(in);
      for (uint32_t e = 0; e < n; ++e) {
         const clap_event_header_t *h = in->get(in, e);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID &&
             (h->type == CLAP_EVENT_PARAM_VALUE || h->type == CLAP_EVENT_PARAM_MOD)) {
            const int i = applyParamEvent(h);
            if (i >= 0)
               sendIfChanged(out, h->time, i);
            continue;
         }
         // Notes and other MIDI go straight through; anything else (transport...) must not be echoed.
         if (jclap::core::isNoteEvent(h))
            out->try_push(out, h);
      }
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   double effective(int i) const { return modulated(values_[i].load(), mods_[i].load()); }

   void resetDefaults() {
      for (int i = 0; i < kCcParamCount; ++i) {
         values_[i].store(defaultValue(kCcParams[i]));
         mods_[i].store(0.0);
         touched_[i].store(false);
      }
      midiChannel_.store(1);
      sender_.reset();
   }

   // Applies a param value or modulation event. Returns the CC param index it touched, or -1.
   int applyParamEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return -1;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id == kChannelParamId) {
            midiChannel_.store(std::clamp(static_cast<int>(std::lround(ev->value)), 1, 16));
            return -1;
         }
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
            values_[i].store(std::clamp(ev->value, 0.0, 127.0));
            touched_[i].store(true);
         }
         return i;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
            mods_[i].store(ev->amount);
            touched_[i].store(true);
         }
         return i;
      }
      return -1;
   }

   void sendIfChanged(const clap_output_events_t *out, uint32_t time, int i) {
      const int v = sender_.update(i, effective(i), touched_[i].load());
      if (v >= 0)
         jclap::core::pushMidi(out, time, static_cast<uint8_t>(0xB0 | (sentChannel_ - 1)),
                               static_cast<uint8_t>(kCcParams[i].cc), static_cast<uint8_t>(v));
   }

   const clap_host_params_t *hostParams_ = nullptr;

   std::atomic<double> values_[kCcParamCount];
   std::atomic<double> mods_[kCcParamCount];
   // A CC is only sent once something has set it; see CcSender.
   std::atomic<bool> touched_[kCcParamCount];
   std::atomic<int> midiChannel_{1};
   std::atomic<bool> resendRequested_{false};

   // Audio thread only.
   CcSender sender_;
   int sentChannel_ = 1;

   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t MicroMonsta::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.micromonsta2",
   "MicroMonsta 2 CC",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.5.0",
   "Knob panel that sends every MicroMonsta 2 parameter as MIDI CC",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(MicroMonsta)
