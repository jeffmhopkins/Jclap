// JClap Orb Polar: a GUI-less CLAP note effect that turns two XY CCs into Omnisphere Orb
// angle/radius CCs. Settings are exposed as plain parameters so the host shows them generically.

#include <clap/clap.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "polar.h"

namespace {

enum ParamId : clap_id {
   kParamInX = 0,
   kParamInY,
   kParamOutAngle,
   kParamOutRadius,
   kParamAngleOffset,
   kParamDirection,
   kParamRadiusMode,
   kParamPassThrough,
   kParamCount
};

struct ParamInfo {
   const char *name;
   double min, max, def;
   bool stepped;
};

const ParamInfo kParams[kParamCount] = {
   {"X In CC", 0, 127, 16, true},
   {"Y In CC", 0, 127, 17, true},
   {"Angle Out CC", 0, 127, 18, true},
   {"Radius Out CC", 0, 127, 19, true},
   {"Angle Offset", 0, 360, 0, false},
   {"Direction", 0, 1, 0, true},
   {"Radius Mode", 0, 1, 0, true},
   {"Pass Input CCs", 0, 1, 0, true},
};

struct ChannelState {
   int x = 64, y = 64;
   int lastAngle = -1, lastRadius = -1;
};

struct OrbPolar {
   clap_plugin_t plugin;
   const clap_host_t *host;
   std::atomic<double> params[kParamCount];
   ChannelState channels[16];

   void resetChannels() {
      for (auto &c : channels)
         c = ChannelState{};
   }

   int intParam(ParamId id) const { return static_cast<int>(params[id].load() + 0.5); }

   void applyParamEvents(const clap_input_events_t *in) {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
            applyParam(reinterpret_cast<const clap_event_param_value_t *>(h));
      }
   }

   void applyParam(const clap_event_param_value_t *ev) {
      if (ev->param_id < kParamCount)
         params[ev->param_id].store(ev->value);
   }

   void pushCC(const clap_output_events_t *out, uint32_t time, int channel, int cc, int value) {
      clap_event_midi_t ev{};
      ev.header.size = sizeof(ev);
      ev.header.time = time;
      ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      ev.header.type = CLAP_EVENT_MIDI;
      ev.port_index = 0;
      ev.data[0] = static_cast<uint8_t>(0xB0 | channel);
      ev.data[1] = static_cast<uint8_t>(cc);
      ev.data[2] = static_cast<uint8_t>(value);
      out->try_push(out, &ev.header);
   }

   // Returns true if the event was an XY input CC and should not be forwarded.
   bool handleMidi(const clap_event_midi_t *ev, const clap_output_events_t *out) {
      if ((ev->data[0] & 0xF0) != 0xB0)
         return false;
      const int channel = ev->data[0] & 0x0F;
      const int cc = ev->data[1];
      const int value = ev->data[2];
      ChannelState &st = channels[channel];

      if (cc == intParam(kParamInX))
         st.x = value;
      else if (cc == intParam(kParamInY))
         st.y = value;
      else
         return false;

      const bool passThrough = intParam(kParamPassThrough) != 0;
      if (passThrough)
         out->try_push(out, &ev->header);

      jclap::PolarSettings s;
      s.angleOffsetDeg = params[kParamAngleOffset].load();
      s.clockwise = intParam(kParamDirection) != 0;
      s.radiusMode = static_cast<jclap::RadiusMode>(intParam(kParamRadiusMode));
      const jclap::PolarCC p = jclap::toPolar(st.x, st.y, s);

      // At dead center the angle is undefined, so it holds its last value.
      if (p.hasAngle && p.angle != st.lastAngle) {
         pushCC(out, ev->header.time, channel, intParam(kParamOutAngle), p.angle);
         st.lastAngle = p.angle;
      }
      if (p.radius != st.lastRadius) {
         pushCC(out, ev->header.time, channel, intParam(kParamOutRadius), p.radius);
         st.lastRadius = p.radius;
      }
      return true;
   }

   clap_process_status process(const clap_process_t *proc) {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            if (h->type == CLAP_EVENT_PARAM_VALUE) {
               applyParam(reinterpret_cast<const clap_event_param_value_t *>(h));
               continue;
            }
            if (h->type == CLAP_EVENT_PARAM_MOD || h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                h->type == CLAP_EVENT_PARAM_GESTURE_END)
               continue;
            if (h->type == CLAP_EVENT_MIDI &&
                handleMidi(reinterpret_cast<const clap_event_midi_t *>(h), out))
               continue;
         }
         // Everything else (notes, other CCs, sysex...) goes straight through.
         out->try_push(out, h);
      }
      return CLAP_PROCESS_CONTINUE;
   }
};

OrbPolar *self(const clap_plugin_t *p) { return static_cast<OrbPolar *>(p->plugin_data); }

// ---- note ports ----

uint32_t notePortsCount(const clap_plugin_t *, bool) { return 1; }

bool notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_note_port_info_t *info) {
   if (index != 0)
      return false;
   info->id = 0;
   info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
   info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
   std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "XY In" : "Orb Out");
   return true;
}

const clap_plugin_note_ports_t kNotePorts = {notePortsCount, notePortsGet};

// ---- params ----

uint32_t paramsCount(const clap_plugin_t *) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t *, uint32_t index, clap_param_info_t *info) {
   if (index >= kParamCount)
      return false;
   const ParamInfo &p = kParams[index];
   std::memset(info, 0, sizeof(*info));
   info->id = index;
   info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED : 0);
   std::snprintf(info->name, sizeof(info->name), "%s", p.name);
   info->min_value = p.min;
   info->max_value = p.max;
   info->default_value = p.def;
   return true;
}

bool paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *value) {
   if (id >= kParamCount)
      return false;
   *value = self(plugin)->params[id].load();
   return true;
}

bool paramsValueToText(const clap_plugin_t *, clap_id id, double value, char *out, uint32_t size) {
   if (id >= kParamCount)
      return false;
   const bool on = value >= 0.5;
   switch (id) {
   case kParamAngleOffset:
      std::snprintf(out, size, "%.1f deg", value);
      break;
   case kParamDirection:
      std::snprintf(out, size, "%s", on ? "Clockwise" : "Counter-clockwise");
      break;
   case kParamRadiusMode:
      std::snprintf(out, size, "%s", on ? "Fill square" : "Clamp to circle");
      break;
   case kParamPassThrough:
      std::snprintf(out, size, "%s", on ? "On" : "Off");
      break;
   default:
      std::snprintf(out, size, "CC %d", static_cast<int>(value + 0.5));
      break;
   }
   return true;
}

bool paramsTextToValue(const clap_plugin_t *, clap_id id, const char *text, double *value) {
   if (id >= kParamCount)
      return false;
   const char *s = text;
   if (std::strncmp(s, "CC", 2) == 0)
      s += 2;
   double v;
   if (std::sscanf(s, "%lf", &v) != 1)
      return false;
   *value = v;
   return true;
}

void paramsFlush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *) {
   self(plugin)->applyParamEvents(in);
}

const clap_plugin_params_t kParamsExt = {paramsCount, paramsGetInfo, paramsGetValue,
                                         paramsValueToText, paramsTextToValue, paramsFlush};

// ---- state ----

bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
   double values[kParamCount];
   for (uint32_t i = 0; i < kParamCount; ++i)
      values[i] = self(plugin)->params[i].load();
   const char *buf = reinterpret_cast<const char *>(values);
   int64_t remaining = sizeof(values);
   while (remaining > 0) {
      const int64_t written = stream->write(stream, buf, static_cast<uint64_t>(remaining));
      if (written <= 0)
         return false;
      buf += written;
      remaining -= written;
   }
   return true;
}

bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
   double values[kParamCount];
   char *buf = reinterpret_cast<char *>(values);
   int64_t remaining = sizeof(values);
   while (remaining > 0) {
      const int64_t got = stream->read(stream, buf, static_cast<uint64_t>(remaining));
      if (got <= 0)
         return false;
      buf += got;
      remaining -= got;
   }
   for (uint32_t i = 0; i < kParamCount; ++i)
      self(plugin)->params[i].store(values[i]);
   return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---- plugin ----

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.orb-polar",
   "Orb Polar",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.1.1",
   "Converts XY CCs to Omnisphere Orb angle and radius CCs",
   kFeatures,
};

bool pluginInit(const clap_plugin_t *plugin) {
   OrbPolar *p = self(plugin);
   for (uint32_t i = 0; i < kParamCount; ++i)
      p->params[i].store(kParams[i].def);
   p->resetChannels();
   return true;
}

void pluginDestroy(const clap_plugin_t *plugin) { delete self(plugin); }

bool pluginActivate(const clap_plugin_t *plugin, double, uint32_t, uint32_t) {
   self(plugin)->resetChannels();
   return true;
}

void pluginDeactivate(const clap_plugin_t *) {}
bool pluginStartProcessing(const clap_plugin_t *) { return true; }
void pluginStopProcessing(const clap_plugin_t *) {}
void pluginReset(const clap_plugin_t *plugin) { self(plugin)->resetChannels(); }

clap_process_status pluginProcess(const clap_plugin_t *plugin, const clap_process_t *process) {
   return self(plugin)->process(process);
}

const void *pluginGetExtension(const clap_plugin_t *, const char *id) {
   if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
      return &kNotePorts;
   if (!std::strcmp(id, CLAP_EXT_PARAMS))
      return &kParamsExt;
   if (!std::strcmp(id, CLAP_EXT_STATE))
      return &kState;
   return nullptr;
}

void pluginOnMainThread(const clap_plugin_t *) {}

const clap_plugin_t *createPlugin(const clap_host_t *host) {
   auto *p = new OrbPolar();
   p->host = host;
   p->plugin.desc = &kDescriptor;
   p->plugin.plugin_data = p;
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
