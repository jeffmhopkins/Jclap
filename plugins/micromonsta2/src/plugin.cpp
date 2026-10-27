// JClap MicroMonsta 2: a CLAP note effect that exposes every MicroMonsta 2 sound parameter as a host
// parameter and sends it out as a MIDI CC whenever it changes. Includes a knob panel (editor.cpp).

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
// Version 2 adds int32 program (0 = none), the program's routing as {source, destination} bytes for
// each mod matrix slot, and the preset name as uint32 length + bytes.
constexpr char kStateMagic[4] = {'J', 'M', 'M', '2'};
constexpr uint32_t kStateVersion = 2;

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
      hostState_ = hostExtension<clap_host_state_t>(CLAP_EXT_STATE);
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      resetDefaults();
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      // Resend everything that has been set, so a reopened project restores the synth's sound.
      resendRequested_.store(true);
      minGap_ = static_cast<int64_t>(sampleRate * kMinGapSeconds);
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
      if (!std::isfinite(value))
         return;
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
      fromProgram_[i].store(false);
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
      // Every knob now goes out when the project opens, which is saved state the host can't see.
      if (hostState_)
         hostState_->mark_dirty(host());
      if (hostParams_)
         hostParams_->request_flush(host());
   }

   void loadProgram(const HardwarePatch &patch) override {
      if (patch.program < 1 || patch.program > kProgramCount)
         return;
      for (int s = 0; s < kModSlots; ++s)
         routes_[s] = patch.routes[s];
      program_ = patch.program;
      presetName_ = displayName(patch);

      // The knobs the program sets stop sending before their values change, and the audio thread
      // counts them as sent once the Program Change is out, so none of them goes out on its own.
      for (int i = 0; i < kCcParamCount; ++i)
         fromProgram_[i].store(false);
      for (auto [cc, v] : patch.values) {
         const int i = indexOfCc(cc);
         if (i < 0 || kCcParams[i].section == Section::Performance)
            continue;
         touched_[i].store(false);
         fromProgram_[i].store(true);
         const double value = std::clamp(static_cast<double>(v), 0.0, 127.0);
         values_[i].store(value);
         edits_.begin(static_cast<clap_id>(cc));
         edits_.value(static_cast<clap_id>(cc), value);
         edits_.end(static_cast<clap_id>(cc));
      }
      programRequested_.store(patch.program);
      if (hostState_)
         hostState_->mark_dirty(host());
      if (hostParams_) {
         // The mod matrix params are named after the routing.
         hostParams_->rescan(host(), CLAP_PARAM_RESCAN_INFO);
         hostParams_->request_flush(host());
      }
   }

   int program() const override { return program_; }
   ModRoute route(int slot) const override { return slot >= 0 && slot < kModSlots ? routes_[slot] : ModRoute{}; }
   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }

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
      // A mod matrix slot is named after the loaded program's routing, e.g. "Mod 1: LFO 2 → Pitch".
      std::string name = p.name;
      if (p.cc >= 52 && p.cc < 52 + kModSlots)
         name = modParamName(p.cc - 52, program_ > 0, routes_[p.cc - 52]);
      jclap::core::fillParamInfo({static_cast<clap_id>(p.cc), name.c_str(), sectionName(p.section), 0, 127,
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
      if (std::sscanf(s, "%lf", &v) != 1 || !std::isfinite(v))
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
      const int32_t program = program_;
      uint8_t routes[2 * kModSlots];
      for (int s = 0; s < kModSlots; ++s) {
         routes[2 * s] = routes_[s].source;
         routes[2 * s + 1] = routes_[s].destination;
      }
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &program, sizeof(program)) &&
             jclap::core::writeAll(stream, routes, sizeof(routes)) &&
             jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      StateHeader header{};
      if (!jclap::core::readAll(stream, &header, sizeof(header)))
         return false;
      if (std::memcmp(header.magic, kStateMagic, sizeof(kStateMagic)) || header.version < 1 || header.version > kStateVersion ||
          header.count > 128)
         return false;

      StateEntry entries[128];
      for (uint32_t i = 0; i < header.count; ++i) {
         if (!jclap::core::readAll(stream, &entries[i], sizeof(StateEntry)))
            return false;
      }
      int32_t program = 0;
      uint8_t routes[2 * kModSlots] = {};
      std::string name;
      if (header.version >= 2) {
         uint32_t nameLength = 0;
         char buffer[256];
         if (!jclap::core::readAll(stream, &program, sizeof(program)) ||
             !jclap::core::readAll(stream, routes, sizeof(routes)) ||
             !jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) || nameLength >= sizeof(buffer) ||
             !jclap::core::readAll(stream, buffer, nameLength))
            return false;
         name.assign(buffer, nameLength);
      }

      // A knob the state leaves out (or saved as NaN) is back at its default, untouched, rather than
      // keeping the previous sound's setting or losing the whole state.
      double value[kCcParamCount];
      bool touched[kCcParamCount];
      for (int i = 0; i < kCcParamCount; ++i) {
         value[i] = defaultValue(kCcParams[i]);
         touched[i] = false;
      }
      for (uint32_t e = 0; e < header.count; ++e) {
         const int i = indexOfCc(entries[e].cc);
         if (i < 0 || !std::isfinite(entries[e].value))
            continue;
         value[i] = std::clamp(entries[e].value, 0.0, 127.0);
         touched[i] = entries[e].touched != 0;
      }
      // The audio thread may be sending meanwhile: a knob going untouched stops sending before its
      // value changes, and one being set gets its value before it starts, so a knob never goes out
      // with a value it doesn't have.
      for (int i = 0; i < kCcParamCount; ++i) {
         if (!touched[i])
            touched_[i].store(false);
         values_[i].store(value[i]);
         if (touched[i])
            touched_[i].store(true);
      }
      midiChannel_.store(std::clamp(static_cast<int>(header.channel), 1, 16));
      program_ = program >= 1 && program <= kProgramCount ? program : 0;
      for (int s = 0; s < kModSlots; ++s)
         routes_[s] = ModRoute{routes[2 * s], routes[2 * s + 1]};
      presetName_ = name;
      // Send the restored sound to the synth on the next block: its program first, then every knob
      // that was set, in case one moved after the program was loaded.
      for (auto &f : fromProgram_)
         f.store(false);
      if (program_)
         programRequested_.store(program_);
      loaded_.store(true);
      resendRequested_.store(true);

      // Tell the host the values changed so it refreshes what it shows.
      if (hostParams_)
         hostParams_->rescan(host(), CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_INFO);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;

      edits_.drain(out);

      // Notes the host's queue refused last block go first, in order.
      retryDeferred(out);

      // Panel edits, state loads and channel changes land between blocks; send them at the start.
      const int ch = midiChannel_.load();
      // After a load, a knob only modulation was sending goes quiet again unless it's still modulated.
      if (loaded_.exchange(false))
         for (int i = 0; i < kCcParamCount; ++i)
            modulatedOnce_[i] = modulatedOnce_[i] && mods_[i].load() != 0.0;
      if (const int program = programRequested_.exchange(0); program > 0 && !sendProgram(out, ch, program))
         programRequested_.store(program);
      const bool resend = resendRequested_.exchange(false);
      if (ch != sentChannel_ || resend) {
         sentChannel_ = ch;
         sender_.resendAll();
      }
      for (int i = 0; i < kCcParamCount; ++i)
         sendIfChanged(out, 0, i, false);

      const uint32_t n = in->size(in);
      for (uint32_t e = 0; e < n; ++e) {
         const clap_event_header_t *h = in->get(in, e);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID &&
             (h->type == CLAP_EVENT_PARAM_VALUE || h->type == CLAP_EVENT_PARAM_MOD)) {
            const int i = applyParamEvent(h);
            if (i >= 0) {
               sendIfChanged(out, h->time, i, h->type == CLAP_EVENT_PARAM_MOD);
            } else if (midiChannel_.load() != sentChannel_) {
               // The channel changed: from here on, every set CC goes out on the new one.
               sentChannel_ = midiChannel_.load();
               sender_.resendAll();
               for (int k = 0; k < kCcParamCount; ++k)
                  sendIfChanged(out, h->time, k, false);
            }
            continue;
         }
         // Notes and other MIDI go straight through; anything else (transport...) must not be echoed.
         if (jclap::core::isNoteEvent(h)) {
            if (h->type == CLAP_EVENT_MIDI)
               heardMidi(reinterpret_cast<const clap_event_midi_t *>(h));
            // Once one is refused, the rest wait behind it so they stay in order.
            if (deferredCount_ > 0 || !out->try_push(out, h))
               defer(h);
         }
      }
      frame_ += proc->frames_count;
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   double effective(int i) const { return modulated(values_[i].load(), mods_[i].load()); }

   // Bank Select and Program Change for `program`, on the channel the CCs go out on. The knobs the
   // program set now match the synth, so they count as sent. False if the host's queue was full.
   bool sendProgram(const clap_output_events_t *out, int ch, int program) {
      const auto status = static_cast<uint8_t>(0xB0 | (ch - 1));
      if (!jclap::core::pushMidi(out, 0, status, 0, 0) ||
          !jclap::core::pushMidi(out, 0, status, 32, static_cast<uint8_t>(bankOf(program))) ||
          !jclap::core::pushMidi(out, 0, static_cast<uint8_t>(0xC0 | (ch - 1)),
                                 static_cast<uint8_t>(programChangeOf(program)), 0))
         return false;
      for (int i = 0; i < kCcParamCount; ++i) {
         if (!fromProgram_[i].exchange(false))
            continue;
         sender_.sent(i, toCcValue(effective(i)));
         touched_[i].store(true);
      }
      return true;
   }

   // A controller's CC passed through on our channel: the synth now has that value, and the knob
   // doesn't go out again until it moves.
   void heardMidi(const clap_event_midi_t *ev) {
      if ((ev->data[0] & 0xF0) != 0xB0 || (ev->data[0] & 0x0F) != sentChannel_ - 1)
         return;
      const int i = indexOfCc(ev->data[1]);
      if (i >= 0)
         sender_.heard(i, ev->data[2] & 0x7F);
   }

   // A refused note or MIDI event is kept for the next block rather than dropped, so a note-off
   // isn't lost. SysEx isn't kept: its data only lives for this block.
   void defer(const clap_event_header_t *h) {
      if (deferredCount_ >= kMaxDeferred || h->size > sizeof(Deferred::bytes) || h->type == CLAP_EVENT_MIDI_SYSEX)
         return;
      std::memcpy(deferred_[deferredCount_++].bytes, h, h->size);
   }

   void retryDeferred(const clap_output_events_t *out) {
      int done = 0;
      while (done < deferredCount_) {
         auto *h = reinterpret_cast<clap_event_header_t *>(deferred_[done].bytes);
         h->time = 0;
         if (!out->try_push(out, h))
            break;
         ++done;
      }
      if (done > 0) {
         std::memmove(deferred_, deferred_ + done, sizeof(Deferred) * (deferredCount_ - done));
         deferredCount_ -= done;
      }
   }

   void resetDefaults() {
      for (int i = 0; i < kCcParamCount; ++i) {
         values_[i].store(defaultValue(kCcParams[i]));
         mods_[i].store(0.0);
         touched_[i].store(false);
      }
      midiChannel_.store(1);
      sender_.reset();
      for (int64_t &f : lastSendFrame_)
         f = INT64_MIN / 2;
   }

   // Applies a param value or modulation event. Returns the CC param index it touched, or -1.
   int applyParamEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return -1;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         // A NaN would be saved, and then fail the whole project's load.
         if (!std::isfinite(ev->value))
            return -1;
         if (ev->param_id == kChannelParamId) {
            midiChannel_.store(std::clamp(static_cast<int>(std::lround(ev->value)), 1, 16));
            return -1;
         }
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
            fromProgram_[i].store(false);
            values_[i].store(std::clamp(ev->value, 0.0, 127.0));
            touched_[i].store(true);
         }
         return i;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         const int i = std::isfinite(ev->amount) ? indexOfCc(static_cast<int>(ev->param_id)) : -1;
         if (i >= 0) {
            mods_[i].store(ev->amount);
            // Sent from now on, without changing the saved state (which only the host's own edits do).
            modulatedOnce_[i] = true;
         }
         return i;
      }
      return -1;
   }

   // `limited` (modulation) sends keep at least kMinGapSeconds between one CC's messages, so fast
   // modulation can't swamp the synth's 5-pin MIDI. A value held back goes out with the next
   // block's sweep, which isn't limited.
   void sendIfChanged(const clap_output_events_t *out, uint32_t time, int i, bool limited) {
      const int64_t at = frame_ + time;
      if (limited && at - lastSendFrame_[i] < minGap_)
         return;
      const int v = sender_.pending(i, effective(i), touched_[i].load() || modulatedOnce_[i]);
      if (v >= 0 && jclap::core::pushMidi(out, time, static_cast<uint8_t>(0xB0 | (sentChannel_ - 1)),
                                          static_cast<uint8_t>(kCcParams[i].cc), static_cast<uint8_t>(v))) {
         sender_.sent(i, v);
         lastSendFrame_[i] = at;
      }
   }

   const clap_host_params_t *hostParams_ = nullptr;
   const clap_host_state_t *hostState_ = nullptr;

   std::atomic<double> values_[kCcParamCount];
   std::atomic<double> mods_[kCcParamCount];
   // A CC is only sent once something has set it; see CcSender.
   std::atomic<bool> touched_[kCcParamCount];
   std::atomic<int> midiChannel_{1};
   std::atomic<bool> resendRequested_{false}, loaded_{false};
   // A Program Change for the audio thread to send (0 = none), and the knobs that program set.
   std::atomic<int> programRequested_{0};
   std::atomic<bool> fromProgram_[kCcParamCount] = {};

   // Main thread only.
   int program_ = 0;
   ModRoute routes_[kModSlots];
   std::string presetName_;

   // Audio thread only.
   static constexpr double kMinGapSeconds = 0.005;
   static constexpr int kMaxDeferred = 256;
   struct Deferred {
      alignas(8) uint8_t bytes[64];
   };
   CcSender sender_;
   int sentChannel_ = 1;
   bool modulatedOnce_[kCcParamCount] = {};
   int64_t frame_ = 0, minGap_ = 240;
   int64_t lastSendFrame_[kCcParamCount];
   Deferred deferred_[kMaxDeferred];
   int deferredCount_ = 0;

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
   "0.10.4",
   "Knob panel that sends every MicroMonsta 2 parameter as MIDI CC",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(MicroMonsta)
