#include "jclap/core/plugin.h"

#include <cstdio>
#include <cstdlib>

namespace jclap::core {

Plugin::Plugin(const clap_plugin_descriptor_t *descriptor, const clap_host_t *host) : host_(host) {
   plugin_.desc = descriptor;
   plugin_.plugin_data = this;
   plugin_.init = [](const clap_plugin_t *p) { return from(p)->init(); };
   plugin_.destroy = [](const clap_plugin_t *p) { delete from(p); };
   plugin_.activate = [](const clap_plugin_t *p, double sr, uint32_t minFrames, uint32_t maxFrames) {
      return from(p)->activate(sr, minFrames, maxFrames);
   };
   plugin_.deactivate = [](const clap_plugin_t *p) { from(p)->deactivate(); };
   plugin_.start_processing = [](const clap_plugin_t *p) { return from(p)->startProcessing(); };
   plugin_.stop_processing = [](const clap_plugin_t *p) { from(p)->stopProcessing(); };
   plugin_.reset = [](const clap_plugin_t *p) { from(p)->reset(); };
   plugin_.process = [](const clap_plugin_t *p, const clap_process_t *process) { return from(p)->process(process); };
   plugin_.get_extension = [](const clap_plugin_t *p, const char *id) { return from(p)->extension(id); };
   plugin_.on_main_thread = [](const clap_plugin_t *p) { from(p)->onMainThread(); };
}

bool Plugin::paramsValueToText(clap_id, double value, char *out, uint32_t size) const {
   std::snprintf(out, size, "%g", value);
   return true;
}

bool Plugin::paramsTextToValue(clap_id, const char *text, double *value) const {
   char *end = nullptr;
   const double v = std::strtod(text, &end);
   if (end == text)
      return false;
   *value = v;
   return true;
}

bool Plugin::notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const {
   if (index != 0)
      return false;
   info->id = 0;
   info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
   info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
   std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes In" : "Notes Out");
   return true;
}

bool Plugin::audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const {
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

const clap_plugin_params_t Plugin::paramsExtension = {
   [](const clap_plugin_t *p) { return from(p)->paramsCount(); },
   [](const clap_plugin_t *p, uint32_t index, clap_param_info_t *info) { return from(p)->paramsInfo(index, info); },
   [](const clap_plugin_t *p, clap_id id, double *value) { return from(p)->paramsValue(id, value); },
   [](const clap_plugin_t *p, clap_id id, double value, char *out, uint32_t size) {
      return from(p)->paramsValueToText(id, value, out, size);
   },
   [](const clap_plugin_t *p, clap_id id, const char *text, double *value) {
      return from(p)->paramsTextToValue(id, text, value);
   },
   [](const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
      from(p)->paramsFlush(in, out);
   },
};

const clap_plugin_state_t Plugin::stateExtension = {
   [](const clap_plugin_t *p, const clap_ostream_t *s) { return from(p)->stateSave(s); },
   [](const clap_plugin_t *p, const clap_istream_t *s) { return from(p)->stateLoad(s); },
};

const clap_plugin_note_ports_t Plugin::notePortsExtension = {
   [](const clap_plugin_t *p, bool isInput) { return from(p)->notePortsCount(isInput); },
   [](const clap_plugin_t *p, uint32_t index, bool isInput, clap_note_port_info_t *info) {
      return from(p)->notePortsInfo(index, isInput, info);
   },
};

const clap_plugin_audio_ports_t Plugin::audioPortsExtension = {
   [](const clap_plugin_t *p, bool isInput) { return from(p)->audioPortsCount(isInput); },
   [](const clap_plugin_t *p, uint32_t index, bool isInput, clap_audio_port_info_t *info) {
      return from(p)->audioPortsInfo(index, isInput, info);
   },
};

} // namespace jclap::core
