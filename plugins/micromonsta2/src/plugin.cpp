// JClap MicroMonsta 2: a CLAP note effect that exposes every MicroMonsta 2 sound parameter as a host
// parameter and sends it out as a MIDI CC whenever it changes. Includes a knob panel (editor.cpp).

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include <jclap/ui/clap_gui.h>

#include "cc_map.h"
#include "editor.h"

namespace {

using namespace mm2;

// Single-producer (GUI thread) single-consumer (audio thread) queue of panel edits, which the audio
// thread turns into CLAP param events so the host records them.
struct GuiEvent {
   enum Kind : uint8_t { Begin, Value, End } kind;
   clap_id id;
   double value;
};

class GuiQueue {
public:
   bool push(const GuiEvent &e) {
      const uint32_t w = write_.load(std::memory_order_relaxed);
      const uint32_t next = (w + 1) % kSize;
      if (next == read_.load(std::memory_order_acquire))
         return false;
      events_[w] = e;
      write_.store(next, std::memory_order_release);
      return true;
   }

   bool pop(GuiEvent &e) {
      const uint32_t r = read_.load(std::memory_order_relaxed);
      if (r == write_.load(std::memory_order_acquire))
         return false;
      e = events_[r];
      read_.store((r + 1) % kSize, std::memory_order_release);
      return true;
   }

private:
   static constexpr uint32_t kSize = 1024;
   std::array<GuiEvent, kSize> events_;
   std::atomic<uint32_t> write_{0}, read_{0};
};

struct MicroMonsta final : EditorHost {
   clap_plugin_t plugin;
   const clap_host_t *host;
   const clap_host_params_t *hostParams = nullptr;

   std::atomic<double> values[kCcParamCount];
   std::atomic<double> mods[kCcParamCount];
   // A CC is only sent once something has set it; see CcSender.
   std::atomic<bool> touched[kCcParamCount];
   std::atomic<int> midiChannel{1};
   std::atomic<bool> resendRequested{false};

   // Audio thread only.
   CcSender sender;
   int sentChannel = 1;

   GuiQueue guiEvents;
   jclap::ui::ClapGui gui;

   double effective(int i) const { return modulated(values[i].load(), mods[i].load()); }

   // ---- EditorHost (main thread) ----

   double baseValue(int index) const override { return values[index].load(); }
   double modulatedValue(int index) const override { return effective(index); }

   void beginEdit(int index) override { queueGui({GuiEvent::Begin, clapId(index), 0}); }

   void setValue(int index, double value) override {
      value = std::clamp(value, 0.0, 127.0);
      values[index].store(value);
      touched[index].store(true);
      queueGui({GuiEvent::Value, clapId(index), value});
   }

   void endEdit(int index) override { queueGui({GuiEvent::End, clapId(index), 0}); }

   int channel() const override { return midiChannel.load(); }

   void setChannel(int ch) override {
      ch = std::clamp(ch, 1, 16);
      midiChannel.store(ch);
      queueGui({GuiEvent::Begin, kChannelParamId, 0});
      queueGui({GuiEvent::Value, kChannelParamId, static_cast<double>(ch)});
      queueGui({GuiEvent::End, kChannelParamId, 0});
   }

   void sendAll() override {
      for (auto &t : touched)
         t.store(true);
      resendRequested.store(true);
      if (hostParams)
         hostParams->request_flush(host);
   }

   static clap_id clapId(int index) { return static_cast<clap_id>(kCcParams[index].cc); }

   void queueGui(const GuiEvent &e) {
      guiEvents.push(e);
      if (hostParams)
         hostParams->request_flush(host);
   }

   // ---- audio thread ----

   void resetDefaults() {
      for (int i = 0; i < kCcParamCount; ++i) {
         values[i].store(defaultValue(kCcParams[i]));
         mods[i].store(0.0);
         touched[i].store(false);
      }
      midiChannel.store(1);
      sender.reset();
   }

   // Turns queued panel edits into param events for the host.
   void drainGui(const clap_output_events_t *out) {
      GuiEvent e;
      while (guiEvents.pop(e)) {
         if (e.kind == GuiEvent::Value) {
            clap_event_param_value_t ev{};
            ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            ev.param_id = e.id;
            ev.note_id = -1;
            ev.port_index = -1;
            ev.channel = -1;
            ev.key = -1;
            ev.value = e.value;
            out->try_push(out, &ev.header);
         } else {
            clap_event_param_gesture_t ev{};
            ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID,
                         static_cast<uint16_t>(e.kind == GuiEvent::Begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                         : CLAP_EVENT_PARAM_GESTURE_END),
                         0};
            ev.param_id = e.id;
            out->try_push(out, &ev.header);
         }
      }
   }

   // Applies a param value or modulation event. Returns the CC param index it touched, or -1.
   int applyParamEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return -1;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id == kChannelParamId) {
            midiChannel.store(std::clamp(static_cast<int>(std::lround(ev->value)), 1, 16));
            return -1;
         }
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
            values[i].store(std::clamp(ev->value, 0.0, 127.0));
            touched[i].store(true);
         }
         return i;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         const int i = indexOfCc(static_cast<int>(ev->param_id));
         if (i >= 0) {
            mods[i].store(ev->amount);
            touched[i].store(true);
         }
         return i;
      }
      return -1;
   }

   void pushCC(const clap_output_events_t *out, uint32_t time, int cc, int value) {
      clap_event_midi_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
      ev.port_index = 0;
      ev.data[0] = static_cast<uint8_t>(0xB0 | (sentChannel - 1));
      ev.data[1] = static_cast<uint8_t>(cc);
      ev.data[2] = static_cast<uint8_t>(value);
      out->try_push(out, &ev.header);
   }

   void sendIfChanged(const clap_output_events_t *out, uint32_t time, int i) {
      const int v = sender.update(i, effective(i), touched[i].load());
      if (v >= 0)
         pushCC(out, time, kCcParams[i].cc, v);
   }

   static bool isNoteEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return false;
      switch (h->type) {
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF:
      case CLAP_EVENT_NOTE_CHOKE:
      case CLAP_EVENT_NOTE_END:
      case CLAP_EVENT_NOTE_EXPRESSION:
      case CLAP_EVENT_MIDI:
      case CLAP_EVENT_MIDI_SYSEX:
      case CLAP_EVENT_MIDI2:
         return true;
      default:
         return false;
      }
   }

   // Audio is copied through untouched; the port only exists so hosts that expect audio IO accept us.
   static void passAudioThrough(const clap_process_t *proc) {
      if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
         return;
      const clap_audio_buffer_t &in = proc->audio_inputs[0];
      const clap_audio_buffer_t &out = proc->audio_outputs[0];
      const uint32_t channels = std::min(in.channel_count, out.channel_count);
      for (uint32_t c = 0; c < channels; ++c) {
         if (in.data32 && out.data32 && in.data32[c] != out.data32[c])
            std::memcpy(out.data32[c], in.data32[c], proc->frames_count * sizeof(float));
         if (in.data64 && out.data64 && in.data64[c] != out.data64[c])
            std::memcpy(out.data64[c], in.data64[c], proc->frames_count * sizeof(double));
      }
      for (uint32_t c = channels; c < out.channel_count; ++c) {
         if (out.data32)
            std::memset(out.data32[c], 0, proc->frames_count * sizeof(float));
         if (out.data64)
            std::memset(out.data64[c], 0, proc->frames_count * sizeof(double));
      }
   }

   clap_process_status process(const clap_process_t *proc) {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;

      drainGui(out);

      // Panel edits, state loads and channel changes land between blocks; send them at the start.
      const int ch = midiChannel.load();
      if (ch != sentChannel || resendRequested.exchange(false)) {
         sentChannel = ch;
         sender.resendAll();
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
         if (isNoteEvent(h))
            out->try_push(out, h);
      }
      passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }
};

// plugin_data holds the EditorHost view of the plugin, so tests can drive what the panel sees.
MicroMonsta *self(const clap_plugin_t *p) {
   return static_cast<MicroMonsta *>(static_cast<EditorHost *>(p->plugin_data));
}

// ---- note ports ----

uint32_t notePortsCount(const clap_plugin_t *, bool) { return 1; }

bool notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_note_port_info_t *info) {
   if (index != 0)
      return false;
   info->id = 0;
   info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
   info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
   std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes In" : "To MicroMonsta");
   return true;
}

const clap_plugin_note_ports_t kNotePorts = {notePortsCount, notePortsGet};

// ---- audio ports ----

uint32_t audioPortsCount(const clap_plugin_t *, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_audio_port_info_t *info) {
   if (index != 0)
      return false;
   info->id = 0;
   std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Audio In" : "Audio Out");
   info->channel_count = 2;
   info->flags = CLAP_AUDIO_PORT_IS_MAIN;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = 0;
   return true;
}

const clap_plugin_audio_ports_t kAudioPorts = {audioPortsCount, audioPortsGet};

// ---- params ----

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

uint32_t paramsCount(const clap_plugin_t *) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t *, uint32_t index, clap_param_info_t *info) {
   if (index >= static_cast<uint32_t>(kParamCount))
      return false;
   std::memset(info, 0, sizeof(*info));
   if (index == static_cast<uint32_t>(kCcParamCount)) {
      info->id = kChannelParamId;
      info->flags = CLAP_PARAM_IS_STEPPED;
      std::snprintf(info->name, sizeof(info->name), "MIDI Channel");
      std::snprintf(info->module, sizeof(info->module), "Settings");
      info->min_value = 1;
      info->max_value = 16;
      info->default_value = 1;
      return true;
   }
   const CcParam &p = kCcParams[index];
   info->id = static_cast<clap_id>(p.cc);
   info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
   std::snprintf(info->name, sizeof(info->name), "%s", p.name);
   std::snprintf(info->module, sizeof(info->module), "%s", sectionName(p.section));
   info->min_value = 0;
   info->max_value = 127;
   info->default_value = defaultValue(p);
   return true;
}

bool paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *value) {
   MicroMonsta *p = self(plugin);
   if (id == kChannelParamId) {
      *value = p->midiChannel.load();
      return true;
   }
   const int i = indexOfCc(static_cast<int>(id));
   if (i < 0)
      return false;
   *value = p->values[i].load();
   return true;
}

bool paramsValueToText(const clap_plugin_t *, clap_id id, double value, char *out, uint32_t size) {
   if (id == kChannelParamId) {
      std::snprintf(out, size, "Ch %d", static_cast<int>(std::lround(value)));
      return true;
   }
   if (indexOfCc(static_cast<int>(id)) < 0)
      return false;
   std::snprintf(out, size, "%d", toCcValue(value));
   return true;
}

bool paramsTextToValue(const clap_plugin_t *, clap_id id, const char *text, double *value) {
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

void paramsFlush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
   MicroMonsta *p = self(plugin);
   const uint32_t n = in->size(in);
   for (uint32_t i = 0; i < n; ++i)
      p->applyParamEvent(in->get(in, i));
   p->drainGui(out);
}

const clap_plugin_params_t kParamsExt = {paramsCount, paramsGetInfo, paramsGetValue,
                                         paramsValueToText, paramsTextToValue, paramsFlush};

// ---- state ----

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

bool writeAll(const clap_ostream_t *stream, const void *data, int64_t size) {
   const char *buf = static_cast<const char *>(data);
   while (size > 0) {
      const int64_t written = stream->write(stream, buf, static_cast<uint64_t>(size));
      if (written <= 0)
         return false;
      buf += written;
      size -= written;
   }
   return true;
}

bool readAll(const clap_istream_t *stream, void *data, int64_t size) {
   char *buf = static_cast<char *>(data);
   while (size > 0) {
      const int64_t got = stream->read(stream, buf, static_cast<uint64_t>(size));
      if (got <= 0)
         return false;
      buf += got;
      size -= got;
   }
   return true;
}

bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
   MicroMonsta *p = self(plugin);
   StateHeader header{};
   std::memcpy(header.magic, kStateMagic, sizeof(kStateMagic));
   header.version = kStateVersion;
   header.channel = p->midiChannel.load();
   header.count = kCcParamCount;
   if (!writeAll(stream, &header, sizeof(header)))
      return false;
   for (int i = 0; i < kCcParamCount; ++i) {
      const StateEntry e{kCcParams[i].cc, p->touched[i].load() ? 1 : 0, p->values[i].load()};
      if (!writeAll(stream, &e, sizeof(e)))
         return false;
   }
   return true;
}

bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
   StateHeader header{};
   if (!readAll(stream, &header, sizeof(header)))
      return false;
   if (std::memcmp(header.magic, kStateMagic, sizeof(kStateMagic)) || header.version != kStateVersion ||
       header.count > 128)
      return false;

   StateEntry entries[128];
   for (uint32_t i = 0; i < header.count; ++i) {
      if (!readAll(stream, &entries[i], sizeof(StateEntry)) || !std::isfinite(entries[i].value))
         return false;
   }

   MicroMonsta *p = self(plugin);
   p->midiChannel.store(std::clamp(static_cast<int>(header.channel), 1, 16));
   for (uint32_t e = 0; e < header.count; ++e) {
      const int i = indexOfCc(entries[e].cc);
      if (i < 0)
         continue;
      p->values[i].store(std::clamp(entries[e].value, 0.0, 127.0));
      p->touched[i].store(entries[e].touched != 0);
   }
   // Send the restored sound to the synth on the next block.
   p->resendRequested.store(true);

   // Tell the host the values changed so it refreshes what it shows.
   if (p->hostParams)
      p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
   return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---- gui ----

jclap::ui::ClapGui *guiOf(const clap_plugin_t *plugin) { return &self(plugin)->gui; }

// ---- plugin ----

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.micromonsta2",
   "MicroMonsta 2 CC",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.2.0",
   "Knob panel that sends every MicroMonsta 2 parameter as MIDI CC",
   kFeatures,
};

bool pluginInit(const clap_plugin_t *plugin) {
   MicroMonsta *p = self(plugin);
   p->hostParams = static_cast<const clap_host_params_t *>(p->host->get_extension(p->host, CLAP_EXT_PARAMS));
   p->gui.init(p->host);
   p->gui.createWindow = [p] { return createEditor(*p); };
   p->resetDefaults();
   return true;
}

void pluginDestroy(const clap_plugin_t *plugin) {
   self(plugin)->gui.destroy();
   delete self(plugin);
}

bool pluginActivate(const clap_plugin_t *plugin, double, uint32_t, uint32_t) {
   // Resend everything that has been set, so a reopened project restores the synth's sound.
   self(plugin)->resendRequested.store(true);
   return true;
}

void pluginDeactivate(const clap_plugin_t *) {}
bool pluginStartProcessing(const clap_plugin_t *) { return true; }
void pluginStopProcessing(const clap_plugin_t *) {}

void pluginReset(const clap_plugin_t *plugin) {
   MicroMonsta *p = self(plugin);
   for (auto &m : p->mods)
      m.store(0.0);
}

clap_process_status pluginProcess(const clap_plugin_t *plugin, const clap_process_t *process) {
   return self(plugin)->process(process);
}

const void *pluginGetExtension(const clap_plugin_t *, const char *id) {
   if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
      return &kAudioPorts;
   if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
      return &kNotePorts;
   if (!std::strcmp(id, CLAP_EXT_PARAMS))
      return &kParamsExt;
   if (!std::strcmp(id, CLAP_EXT_STATE))
      return &kState;
   return jclap::ui::ClapGui::extension<guiOf>(id);
}

void pluginOnMainThread(const clap_plugin_t *) {}

const clap_plugin_t *createPlugin(const clap_host_t *host) {
   auto *p = new MicroMonsta();
   p->host = host;
   p->plugin.desc = &kDescriptor;
   p->plugin.plugin_data = static_cast<EditorHost *>(p);
   p->plugin.init = pluginInit;
   p->plugin.destroy = pluginDestroy;
   p->plugin.activate = pluginActivate;
   p->plugin.deactivate = pluginDeactivate;
   p->plugin.start_processing = pluginStartProcessing;
   p->plugin.stop_processing = pluginStopProcessing;
   p->plugin.reset = pluginReset;
   p->plugin.process = pluginProcess;
   p->plugin.get_extension = pluginGetExtension;
   p->plugin.on_main_thread = pluginOnMainThread;
   return &p->plugin;
}

// ---- factory / entry ----

uint32_t factoryCount(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *factoryDescriptor(const clap_plugin_factory_t *, uint32_t index) {
   return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *factoryCreate(const clap_plugin_factory_t *, const clap_host_t *host,
                                   const char *pluginId) {
   if (!clap_version_is_compatible(host->clap_version) || std::strcmp(pluginId, kDescriptor.id))
      return nullptr;
   return createPlugin(host);
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char *) { return true; }
void entryDeinit() {}

const void *entryGetFactory(const char *id) {
   return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entryInit,
                                                              entryDeinit, entryGetFactory};
