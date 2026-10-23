#pragma once

// Base class for a CLAP plugin. Override what the plugin needs; every clap_plugin_t callback and the
// common extensions (params, state, note ports, audio ports) are routed to these virtuals.
//
//    class MyPlugin final : public jclap::core::Plugin {
//    public:
//       static const clap_plugin_descriptor_t descriptor;
//       explicit MyPlugin(const clap_host_t *host) : Plugin(&descriptor, host) {}
//       clap_process_status process(const clap_process_t *p) override;
//       const void *extension(const char *id) override {
//          if (!std::strcmp(id, CLAP_EXT_PARAMS))
//             return &paramsExtension;
//          return nullptr;
//       }
//    };
//    JCLAP_ENTRY(MyPlugin) // in exactly one .cpp, see entry.h

#include <clap/clap.h>

namespace jclap::core {

class Plugin {
public:
   Plugin(const clap_plugin_descriptor_t *descriptor, const clap_host_t *host);
   virtual ~Plugin() = default;
   Plugin(const Plugin &) = delete;
   Plugin &operator=(const Plugin &) = delete;

   const clap_plugin_t *clapPlugin() const { return &plugin_; }
   static Plugin *from(const clap_plugin_t *plugin) { return static_cast<Plugin *>(plugin->plugin_data); }

   const clap_host_t *host() const { return host_; }
   // A host extension, or null. Only valid from init() on.
   template <typename T>
   const T *hostExtension(const char *id) const {
      return static_cast<const T *>(host_->get_extension(host_, id));
   }

   // ---- clap_plugin_t ----
   virtual bool init() { return true; }
   virtual bool activate(double /*sampleRate*/, uint32_t /*minFrames*/, uint32_t /*maxFrames*/) { return true; }
   virtual void deactivate() {}
   virtual bool startProcessing() { return true; }
   virtual void stopProcessing() {}
   virtual void reset() {}
   virtual clap_process_status process(const clap_process_t *process) = 0;
   // Return one of the ready-made tables below, or your own.
   virtual const void *extension(const char * /*id*/) { return nullptr; }
   virtual void onMainThread() {}

   // ---- params (paramsExtension) ----
   virtual uint32_t paramsCount() const { return 0; }
   virtual bool paramsInfo(uint32_t /*index*/, clap_param_info_t * /*info*/) const { return false; }
   virtual bool paramsValue(clap_id /*id*/, double * /*value*/) const { return false; }
   virtual bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const;
   virtual bool paramsTextToValue(clap_id id, const char *text, double *value) const;
   virtual void paramsFlush(const clap_input_events_t * /*in*/, const clap_output_events_t * /*out*/) {}

   // ---- state (stateExtension) ----
   virtual bool stateSave(const clap_ostream_t * /*stream*/) { return false; }
   virtual bool stateLoad(const clap_istream_t * /*stream*/) { return false; }

   // ---- note ports (notePortsExtension). Default: one MIDI port each way ----
   virtual uint32_t notePortsCount(bool /*isInput*/) const { return 1; }
   virtual bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const;

   // ---- audio ports (audioPortsExtension). Default: one stereo port each way ----
   virtual uint32_t audioPortsCount(bool /*isInput*/) const { return 1; }
   virtual bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const;

   static const clap_plugin_params_t paramsExtension;
   static const clap_plugin_state_t stateExtension;
   static const clap_plugin_note_ports_t notePortsExtension;
   static const clap_plugin_audio_ports_t audioPortsExtension;

private:
   clap_plugin_t plugin_;
   const clap_host_t *host_;
};

} // namespace jclap::core
