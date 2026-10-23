// Plugin Template: a stereo gain/pan utility with a small panel. It exists to be copied; see
// README.md. Everything plugin-specific is in this file and editor.cpp.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "editor.h"
#include "params.h"

namespace {

using namespace plugin_template;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// State layout: 4-byte magic, uint32 version, then one double per param in kParams order.
constexpr char kStateMagic[4] = {'J', 'T', 'M', 'P'};
constexpr uint32_t kStateVersion = 1;

class PluginTemplate final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit PluginTemplate(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(kParams[i].defaultValue);
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<PluginTemplate, &PluginTemplate::gui_>(id);
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double value) override {
      if (id >= kParamCount)
         return;
      value = std::clamp(value, kParams[id].min, kParams[id].max);
      values_[id].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   float outputPeak() override { return peak_.exchange(0.0f); }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      jclap::core::fillParamInfo(kParams[index], info);
      return true;
   }

   bool paramsValue(clap_id id, double *value) const override {
      if (id >= kParamCount)
         return false;
      *value = values_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, value, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      return id < kParamCount && parseParam(id, text, *value);
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      applyParamEvents(in);
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t version = kStateVersion;
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, &version, 4))
         return false;
      for (size_t i = 0; i < kParamCount; ++i) {
         const double v = values_[i].load();
         if (!jclap::core::writeAll(stream, &v, sizeof(v)))
            return false;
      }
      return true;
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t version = 0;
      double v[kParamCount];
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, &version, 4) || version != kStateVersion ||
          !jclap::core::readAll(stream, v, sizeof(v)))
         return false;
      for (size_t i = 0; i < kParamCount; ++i) {
         if (!std::isfinite(v[i]))
            return false;
      }
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], kParams[i].min, kParams[i].max));
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      edits_.drain(proc->out_events);
      applyParamEvents(proc->in_events);
      if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
         return CLAP_PROCESS_CONTINUE;

      // Constant-power pan on top of the gain; mute wins.
      const double pan = values_[kPan].load();
      const double angle = (pan + 1.0) * 0.25 * 3.14159265358979;
      const float gain = values_[kMute].load() >= 0.5 ? 0.0f
                                                      : static_cast<float>(std::pow(10.0, values_[kGain].load() / 20.0));
      const float g[2] = {gain * static_cast<float>(std::cos(angle) * std::sqrt(2.0)),
                          gain * static_cast<float>(std::sin(angle) * std::sqrt(2.0))};

      const clap_audio_buffer_t &in = proc->audio_inputs[0];
      const clap_audio_buffer_t &out = proc->audio_outputs[0];
      float peak = 0.0f;
      for (uint32_t c = 0; c < std::min(2u, std::min(in.channel_count, out.channel_count)); ++c) {
         if (!in.data32 || !out.data32)
            continue;
         for (uint32_t i = 0; i < proc->frames_count; ++i) {
            out.data32[c][i] = in.data32[c][i] * g[c];
            peak = std::max(peak, std::fabs(out.data32[c][i]));
         }
      }
      // The panel takes the largest peak since it last looked.
      float seen = peak_.load(std::memory_order_relaxed);
      while (peak > seen && !peak_.compare_exchange_weak(seen, peak)) {
      }
      return CLAP_PROCESS_CONTINUE;
   }

private:
   void applyParamEvents(const clap_input_events_t *in) {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE)
            continue;
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id < kParamCount)
            values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
      }
   }

   std::atomic<double> values_[kParamCount];
   std::atomic<float> peak_{0.0f};
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t PluginTemplate::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.plugin-template",
   "Plugin Template",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.2.0",
   "Stereo gain and pan with a level meter",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(PluginTemplate)
