// JClap MatrixBrute: a CLAP note effect that exposes every Arturia MatrixBrute parameter with a MIDI CC
// as a host parameter and sends it out whenever it changes. The synth sends its knobs as CCs too, so
// with its MIDI out routed into the plugin's track the knobs follow the hardware. Includes a knob
// panel (editor.cpp).

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
#include "patches.h"

namespace {

using namespace mb;

const char *sectionName(Section s) {
   switch (s) {
   case Section::Vco1: return "VCO 1";
   case Section::Vco2: return "VCO 2";
   case Section::Vco3: return "VCO 3";
   case Section::AudioMod: return "Audio Mod";
   case Section::Mixer: return "Mixer";
   case Section::Steiner: return "Steiner Filter";
   case Section::Ladder: return "Ladder Filter";
   case Section::Filters: return "Filters";
   case Section::Env1: return "ENV 1";
   case Section::Env2: return "ENV 2";
   case Section::Env3: return "ENV 3";
   case Section::Lfo: return "LFO";
   case Section::Fx: return "Analog FX";
   case Section::Sequencer: return "Sequencer";
   case Section::Performance: return "Performance";
   }
   return "";
}

// State layout: 4-byte magic, uint32 version, int32 channel, int32 fine, int32 follow, uint32 count,
// then per CC parameter {int32 cc, int32 touched, double value}, then the preset name as uint32
// length + bytes. Keyed by CC so the list can change between versions. Version 2 adds int32 preset
// at the end: the synth preset the plugin loaded (0-255), or -1.
constexpr char kStateMagic[4] = {'J', 'M', 'A', 'T'};
constexpr uint32_t kStateVersion = 2;

struct StateHeader {
   char magic[4];
   uint32_t version;
   int32_t channel;
   int32_t fine;
   int32_t follow;
   uint32_t count;
};

struct StateEntry {
   int32_t cc;
   int32_t touched;
   double value;
};

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

class MatrixBrute final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit MatrixBrute(const clap_host_t *host) : Plugin(&descriptor, host) {}

   // ---- plugin ----

   bool init() override {
      hostParams_ = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS);
      hostState_ = hostExtension<clap_host_state_t>(CLAP_EXT_STATE);
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(*this);
      gui_.createWindow = [this] { return createEditor(*this); };
      resetDefaults();
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      // Resend everything that has been set, so a reopened project restores the synth's sound.
      resendRequested_.store(true);
      minGap_ = static_cast<int64_t>(sampleRate * kMinGapSeconds);
      gestureIdle_ = static_cast<int64_t>(sampleRate * kGestureIdleSeconds);
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
      return jclap::ui::ClapGui::extensionFor<MatrixBrute, &MatrixBrute::gui_>(id);
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes and MatrixBrute In" : "To MatrixBrute");
      return true;
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override {
      if (id == kChannelParamId)
         return midiChannel_.load();
      if (id == kFineParamId)
         return fine_.load() ? 1.0 : 0.0;
      if (id == kFollowParamId)
         return follow_.load() ? 1.0 : 0.0;
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
      if (setSetting(id, value)) {
         edits_.value(id, paramValue(id));
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
      // Every knob now goes out when the project opens, which is saved state the host can't see.
      if (hostState_)
         hostState_->mark_dirty(host());
      if (hostParams_)
         hostParams_->request_flush(host());
   }

   bool known(int cc) const override {
      const int i = indexOfCc(cc);
      return i >= 0 && touched_[i].load();
   }
   uint32_t heardCount() const override { return heardCount_.load(); }
   int synthPreset() const override { return synthPreset_.load(); }
   NoteActivity notes() const override { return {heldNotes_.load(), lastNoteOn_.load(), lastNoteOff_.load()}; }

   void loadProgram(const HardwarePatch &patch) override {
      if (patch.preset < 0 || patch.preset >= kPresetCount)
         return;
      loadedPreset_ = patch.preset;
      presetName_ = displayName(patch);

      // The knobs the preset sets stop sending before their values change, and the audio thread
      // counts them as sent once the Program Change is out, so none of them goes out on its own.
      for (auto &f : fromProgram_)
         f.store(false);
      for (auto [cc, v] : patch.values) {
         const int i = indexOfCc(cc);
         if (i < 0)
            continue;
         touched_[i].store(false);
         fromProgram_[i].store(true);
         const double value = std::clamp(v, 0.0, 127.0);
         values_[i].store(value);
         edits_.begin(static_cast<clap_id>(cc));
         edits_.value(static_cast<clap_id>(cc), value);
         edits_.end(static_cast<clap_id>(cc));
      }
      synthPreset_.store(patch.preset);
      programRequested_.store(patch.preset);
      if (hostState_)
         hostState_->mark_dirty(host());
      if (hostParams_)
         hostParams_->request_flush(host());
   }

   std::string presetName() const override { return presetName_; }
   // A preset of the plugin's own (or a rename) isn't one of the synth's.
   void setPresetName(const std::string &name) override {
      presetName_ = name;
      loadedPreset_ = -1;
   }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= static_cast<uint32_t>(kParamCount))
         return false;
      if (index >= static_cast<uint32_t>(kCcParamCount)) {
         using jclap::core::fillParamInfo;
         switch (index - kCcParamCount) {
         case 0: fillParamInfo({kChannelParamId, "MIDI Channel", "Settings", 1, 16, 1, CLAP_PARAM_IS_STEPPED}, info); break;
         case 1: fillParamInfo({kFineParamId, "Send 14-bit", "Settings", 0, 1, 0, CLAP_PARAM_IS_STEPPED}, info); break;
         default: fillParamInfo({kFollowParamId, "Follow Incoming CCs", "Settings", 0, 1, 1, CLAP_PARAM_IS_STEPPED}, info); break;
         }
         return true;
      }
      const CcParam &p = kCcParams[index];
      jclap::core::fillParamInfo({static_cast<clap_id>(p.cc), p.name, sectionName(p.section), 0, 127, defaultValue(p),
                                  CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE},
                                 info);
      return true;
   }

   bool paramsValue(clap_id id, double *value) const override {
      if (!isSetting(id) && indexOfCc(static_cast<int>(id)) < 0)
         return false;
      *value = paramValue(id);
      return true;
   }

   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id == kChannelParamId) {
         std::snprintf(out, size, "Ch %d", static_cast<int>(std::lround(value)));
         return true;
      }
      if (id == kFineParamId || id == kFollowParamId) {
         std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
         return true;
      }
      const int i = indexOfCc(static_cast<int>(id));
      if (i < 0)
         return false;
      if (kCcParams[i].fine && fine_.load())
         std::snprintf(out, size, "%.2f", std::clamp(value, 0.0, 127.0));
      else
         std::snprintf(out, size, "%d", toCcValue(value));
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      if (!isSetting(id) && indexOfCc(static_cast<int>(id)) < 0)
         return false;
      const char *s = text;
      while (*s == ' ')
         ++s;
      if (std::strncmp(s, "Ch", 2) == 0)
         s += 2;
      if (!std::strncmp(s, "On", 2) || !std::strncmp(s, "Off", 3)) {
         *value = s[1] == 'n' ? 1.0 : 0.0;
         return true;
      }
      double v;
      if (!jclap::core::parseNumber(s, v))
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
      header.fine = fine_.load() ? 1 : 0;
      header.follow = follow_.load() ? 1 : 0;
      header.count = kCcParamCount;
      if (!jclap::core::writeAll(stream, &header, sizeof(header)))
         return false;
      for (int i = 0; i < kCcParamCount; ++i) {
         const StateEntry e{kCcParams[i].cc, touched_[i].load() ? 1 : 0, values_[i].load()};
         if (!jclap::core::writeAll(stream, &e, sizeof(e)))
            return false;
      }
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      const int32_t preset = loadedPreset_;
      return jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength) &&
             jclap::core::writeAll(stream, &preset, sizeof(preset));
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
      uint32_t nameLength = 0;
      char buffer[256];
      if (!jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) || nameLength >= sizeof(buffer) ||
          !jclap::core::readAll(stream, buffer, nameLength))
         return false;
      int32_t preset = -1;
      if (header.version >= 2 && !jclap::core::readAll(stream, &preset, sizeof(preset)))
         return false;

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
      fine_.store(header.fine != 0);
      follow_.store(header.follow != 0);
      presetName_.assign(buffer, nameLength);
      loadedPreset_ = preset >= 0 && preset < kPresetCount ? preset : -1;
      // Send the restored sound to the synth on the next block: its preset first, then every knob
      // that was set, in case one moved after the preset was loaded.
      for (auto &f : fromProgram_)
         f.store(false);
      synthPreset_.store(loadedPreset_);
      if (loadedPreset_ >= 0)
         programRequested_.store(loadedPreset_);
      loaded_.store(true);
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

      // Notes the host's queue refused last block go first, in order.
      retryDeferred(out);
      endIdleGestures(out);

      // Panel edits, state loads and setting changes land between blocks; send them at the start.
      // After a load, a knob only modulation was sending goes quiet again unless it's still modulated.
      if (loaded_.exchange(false))
         for (int i = 0; i < kCcParamCount; ++i)
            modulatedOnce_[i] = modulatedOnce_[i] && mods_[i].load() != 0.0;
      syncSettings(resendRequested_.exchange(false));
      if (const int preset = programRequested_.exchange(-1); preset >= 0 && !sendProgram(out, preset))
         programRequested_.store(preset);
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
            } else if (syncSettings(false)) {
               // The channel or fine bytes changed: from here on, every set CC goes out the new way.
               for (int k = 0; k < kCcParamCount; ++k)
                  sendIfChanged(out, h->time, k, false);
            }
            continue;
         }
         // Notes and other MIDI go straight through; anything else (transport...) must not be echoed.
         // With the synth's Local off, its own knobs only reach it this way.
         if (jclap::core::isNoteEvent(h)) {
            trackNotes(h);
            if (h->type == CLAP_EVENT_MIDI)
               heardMidi(reinterpret_cast<const clap_event_midi_t *>(h), out);
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
   bool fineOut(int i) const { return sentFine_ && kCcParams[i].fine; }

   static bool isSetting(clap_id id) { return id == kChannelParamId || id == kFineParamId || id == kFollowParamId; }

   // Stores a settings param; false if `id` isn't one.
   bool setSetting(clap_id id, double value) {
      if (id == kChannelParamId)
         midiChannel_.store(std::clamp(static_cast<int>(std::lround(value)), 1, 16));
      else if (id == kFineParamId)
         fine_.store(value >= 0.5);
      else if (id == kFollowParamId)
         follow_.store(value >= 0.5);
      else
         return false;
      return true;
   }

   // Picks up a new channel or fine setting (or a resend request). True if every CC must go again.
   bool syncSettings(bool resend) {
      const int ch = midiChannel_.load();
      const bool fine = fine_.load();
      if (ch == sentChannel_ && fine == sentFine_ && !resend)
         return false;
      if (ch != sentChannel_)
         receiver_.reset();
      sentChannel_ = ch;
      sentFine_ = fine;
      sender_.resendAll();
      return true;
   }

   // Bank Select and Program Change for `preset`, on the channel the CCs go out on. The knobs the
   // preset set now match the synth, so they count as sent. False if the host's queue was full.
   bool sendProgram(const clap_output_events_t *out, int preset) {
      const auto status = static_cast<uint8_t>(0xB0 | (sentChannel_ - 1));
      if (!jclap::core::pushMidi(out, 0, status, 0, static_cast<uint8_t>(bankSelectOf(preset))) ||
          !jclap::core::pushMidi(out, 0, status, 32, 0) ||
          !jclap::core::pushMidi(out, 0, static_cast<uint8_t>(0xC0 | (sentChannel_ - 1)),
                                 static_cast<uint8_t>(programChangeOf(preset)), 0))
         return false;
      // The synth has these values now, whether or not 14-bit is on.
      for (int i = 0; i < kCcParamCount; ++i) {
         if (!fromProgram_[i].exchange(false))
            continue;
         sender_.sent(i, toWire(effective(i), fineOut(i)));
         touched_[i].store(true);
      }
      return true;
   }

   // A CC on our channel: the synth has that value now. It came from the synth's own panel or from a
   // controller on the track; with Follow on, the knob moves to it and the host hears about it as a
   // gesture, so it can record the move as automation. A Program Change means the synth loaded
   // another preset, so no knob is known until it moves.
   void heardMidi(const clap_event_midi_t *ev, const clap_output_events_t *out) {
      const int status = ev->data[0] & 0xF0;
      if ((ev->data[0] & 0x0F) != sentChannel_ - 1)
         return;
      if (status == 0xB0 && ev->data[1] == 0) {
         heardBank_ = ev->data[2] & 0x7F;
         return;
      }
      if (status == 0xC0) {
         synthPreset_.store((heardBank_ & 15) * 16 + (ev->data[1] & 15));
         for (int i = 0; i < kCcParamCount; ++i)
            touched_[i].store(false);
         sender_.reset();
         return;
      }
      if (status != 0xB0)
         return;
      int i = indexOfCc(ev->data[1]), wire = 0;
      if (i >= 0) {
         wire = receiver_.coarse(i, ev->data[2]);
      } else if ((i = indexOfFineCc(ev->data[1])) >= 0) {
         wire = receiver_.fine(i, ev->data[2]);
      } else {
         return;
      }
      if (!follow_.load()) {
         sender_.heard(i, fineOut(i) ? wire : wire & ~0x7F);
         return;
      }
      const double value = fromWire(wire);
      values_[i].store(value);
      touched_[i].store(true);
      // The synth already has it, so it doesn't go back out.
      sender_.sent(i, toWire(effective(i), fineOut(i)));
      heardCount_.fetch_add(1);
      const auto id = static_cast<clap_id>(kCcParams[i].cc);
      if (!inGesture_[i])
         inGesture_[i] = jclap::core::pushParamGesture(out, id, true, ev->header.time);
      jclap::core::pushParamValue(out, id, value, ev->header.time);
      lastHeardFrame_[i] = frame_ + ev->header.time;
   }

   // Keys down and when they went down and up, for the panel's envelopes. Any channel: the synth's
   // own keys or the host's notes.
   void trackNotes(const clap_event_header_t *h) {
      int change = 0;
      if (h->type == CLAP_EVENT_NOTE_ON) {
         change = 1;
      } else if (h->type == CLAP_EVENT_NOTE_OFF || h->type == CLAP_EVENT_NOTE_CHOKE) {
         change = -1;
      } else if (h->type == CLAP_EVENT_MIDI) {
         const auto *m = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = m->data[0] & 0xF0;
         if (status == 0x90 && m->data[2] > 0)
            change = 1;
         else if (status == 0x80 || status == 0x90)
            change = -1;
      }
      if (change > 0) {
         heldNotes_.fetch_add(1);
         lastNoteOn_.store(noteClock());
      } else if (change < 0 && heldNotes_.load() > 0 && heldNotes_.fetch_sub(1) == 1) {
         lastNoteOff_.store(noteClock());
      }
   }

   // A knob the synth moved ends its gesture once it has been still for a moment.
   void endIdleGestures(const clap_output_events_t *out) {
      for (int i = 0; i < kCcParamCount; ++i)
         if (inGesture_[i] && frame_ - lastHeardFrame_[i] > gestureIdle_ &&
             jclap::core::pushParamGesture(out, static_cast<clap_id>(kCcParams[i].cc), false))
            inGesture_[i] = false;
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
      receiver_.reset();
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
         if (!std::isfinite(ev->value) || setSetting(ev->param_id, ev->value))
            return -1;
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
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
   // block's sweep, which isn't limited. With fine bytes on, the coarse byte goes first and the
   // fine byte after it, on cc + 32.
   void sendIfChanged(const clap_output_events_t *out, uint32_t time, int i, bool limited) {
      const int64_t at = frame_ + time;
      if (limited && at - lastSendFrame_[i] < minGap_)
         return;
      const bool fine = fineOut(i);
      const int w = sender_.pending(i, toWire(effective(i), fine), touched_[i].load() || modulatedOnce_[i]);
      if (w < 0)
         return;
      const auto status = static_cast<uint8_t>(0xB0 | (sentChannel_ - 1));
      const auto cc = static_cast<uint8_t>(kCcParams[i].cc);
      if (!jclap::core::pushMidi(out, time, status, cc, static_cast<uint8_t>(w >> 7)))
         return;
      if (fine && !jclap::core::pushMidi(out, time, status, static_cast<uint8_t>(cc + kFineOffset),
                                         static_cast<uint8_t>(w & 0x7F))) {
         // Only the coarse byte made it: that's what the synth has, and the rest goes next time.
         sender_.heard(i, w & ~0x7F);
         return;
      }
      sender_.sent(i, w);
      lastSendFrame_[i] = at;
   }

   const clap_host_params_t *hostParams_ = nullptr;
   const clap_host_state_t *hostState_ = nullptr;

   std::atomic<double> values_[kCcParamCount];
   std::atomic<double> mods_[kCcParamCount];
   // A CC is only sent once something has set it; see CcSender. The panel shows the rest as unknown.
   std::atomic<bool> touched_[kCcParamCount];
   std::atomic<int> midiChannel_{1};
   std::atomic<bool> fine_{false}, follow_{true};
   std::atomic<bool> resendRequested_{false}, loaded_{false};
   std::atomic<uint32_t> heardCount_{0};
   std::atomic<int> synthPreset_{-1};
   std::atomic<int> heldNotes_{0};
   std::atomic<double> lastNoteOn_{-1}, lastNoteOff_{-1};
   // A synth preset for the audio thread to send (-1 = none), and the knobs that preset set.
   std::atomic<int> programRequested_{-1};
   std::atomic<bool> fromProgram_[kCcParamCount] = {};

   // Main thread only.
   std::string presetName_;
   int loadedPreset_ = -1; // the synth preset the plugin last loaded, kept with the project

   // Audio thread only.
   static constexpr double kMinGapSeconds = 0.005;
   static constexpr double kGestureIdleSeconds = 0.3;
   static constexpr int kMaxDeferred = 256;
   struct Deferred {
      alignas(8) uint8_t bytes[64];
   };
   CcSender sender_;
   CcReceiver receiver_;
   int sentChannel_ = 1;
   int heardBank_ = 0;
   bool sentFine_ = false;
   bool modulatedOnce_[kCcParamCount] = {};
   bool inGesture_[kCcParamCount] = {};
   int64_t frame_ = 0, minGap_ = 240, gestureIdle_ = 14400;
   int64_t lastSendFrame_[kCcParamCount];
   int64_t lastHeardFrame_[kCcParamCount] = {};
   Deferred deferred_[kMaxDeferred];
   int deferredCount_ = 0;

   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t MatrixBrute::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.matrixbrute",
   "MatrixBrute CC",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "Two-way knob panel for the Arturia MatrixBrute: sends every parameter as MIDI CC and follows the synth's knobs",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(MatrixBrute)
