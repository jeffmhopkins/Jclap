// Checks jclap::core with a minimal plugin: the entry/factory, the extension tables, EditQueue and
// the helpers, all through TestHost the way a plugin's own tests use them.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/test_host.h>

#include <atomic>
#include <cstring>

namespace {

constexpr clap_id kLevel = 7;

class Probe final : public jclap::core::Plugin {
public:
   static const clap_plugin_descriptor_t descriptor;
   explicit Probe(const clap_host_t *host) : Plugin(&descriptor, host) {}

   bool init() override {
      edits.init(host());
      return true;
   }

   clap_process_status process(const clap_process_t *p) override {
      edits.drain(p->out_events);
      const uint32_t n = p->in_events->size(p->in_events);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = p->in_events->get(p->in_events, i);
         if (h->type == CLAP_EVENT_PARAM_VALUE)
            level.store(reinterpret_cast<const clap_event_param_value_t *>(h)->value);
         else if (jclap::core::isNoteEvent(h))
            p->out_events->try_push(p->out_events, h);
      }
      jclap::core::passAudioThrough(p);
      return CLAP_PROCESS_CONTINUE;
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      return nullptr;
   }

   uint32_t paramsCount() const override { return 1; }
   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index != 0)
         return false;
      jclap::core::fillParamInfo({kLevel, "Level", "", 0, 10, 5}, info);
      return true;
   }
   bool paramsValue(clap_id id, double *value) const override {
      if (id != kLevel)
         return false;
      *value = level.load();
      return true;
   }
   void paramsFlush(const clap_input_events_t *, const clap_output_events_t *out) override { edits.drain(out); }

   std::atomic<double> level{5.0};
   jclap::core::EditQueue edits;
};

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
const clap_plugin_descriptor_t Probe::descriptor = {CLAP_VERSION_INIT, "test.probe", "Probe", "", "", "", "",
                                                    "1", "", kFeatures};

} // namespace

JCLAP_ENTRY(Probe)

int main() {
   auto *factory = static_cast<const clap_plugin_factory_t *>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
   JCLAP_CHECK_EQ(factory->get_plugin_count(factory), 1u);
   JCLAP_CHECK(!std::strcmp(factory->get_plugin_descriptor(factory, 0)->id, "test.probe"));
   JCLAP_CHECK(factory->get_plugin_descriptor(factory, 1) == nullptr);

   jclap::core::TestHost host(clap_entry, "test.probe");
   auto *probe = host.object<Probe>();

   // Param events reach the plugin and read back through the params extension.
   host.setParam(kLevel, 3.0);
   host.process();
   JCLAP_CHECK_EQ(host.paramValue(kLevel), 3.0);
   JCLAP_CHECK_EQ(host.paramText(kLevel, 2.5) == "2.5", true);

   // GUI edits come out as begin, value, end, from process and from flush.
   probe->edits.set(kLevel, 8.0);
   auto out = host.process();
   JCLAP_CHECK_EQ(out.gestures.size(), 2u);
   JCLAP_CHECK_EQ(out.paramValues.size(), 1u);
   if (out.gestures.size() == 2 && out.paramValues.size() == 1) {
      JCLAP_CHECK_EQ(out.gestures[0].header.type, CLAP_EVENT_PARAM_GESTURE_BEGIN);
      JCLAP_CHECK_EQ(out.gestures[1].header.type, CLAP_EVENT_PARAM_GESTURE_END);
      JCLAP_CHECK_EQ(out.paramValues[0].value, 8.0);
   }
   probe->edits.value(kLevel, 9.0);
   JCLAP_CHECK_EQ(host.flush().paramValues.size(), 1u);
   JCLAP_CHECK_EQ(host.process().paramValues.size(), 0u);

   // Notes pass through; audio is copied.
   host.midi(0x90, 60, 100);
   out = host.process();
   JCLAP_CHECK_EQ(out.midi.size(), 1u);
   float inL[4] = {1, 2, 3, 4}, inR[4] = {5, 6, 7, 8}, outL[4] = {}, outR[4] = {};
   float *in[2] = {inL, inR}, *outs[2] = {outL, outR};
   host.process(4, in, outs);
   JCLAP_CHECK_EQ(outR[3], 8.0f);

   return jclap::core::testResult();
}
