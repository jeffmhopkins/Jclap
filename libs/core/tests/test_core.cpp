// Checks jclap::core with a minimal plugin: the entry/factory, the extension tables, EditQueue and
// the helpers, all through TestHost the way a plugin's own tests use them.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/sample_fifo.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/core/test_host.h>
#include <jclap/core/triple_buffer.h>

#include <atomic>
#include <cstring>
#include <type_traits>

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

// A host for EditQueue on its own: its params extension counts flush requests.
int flushRequests = 0;
const clap_host_params_t kHostParams = {[](const clap_host_t *, clap_param_rescan_flags) {},
                                        [](const clap_host_t *, clap_id, clap_param_clear_flags) {},
                                        [](const clap_host_t *) { ++flushRequests; }};
const clap_host_t kParamsHost = {CLAP_VERSION_INIT, nullptr, "edit test", "jclap", "", "1",
                                 [](const clap_host_t *, const char *id) -> const void * {
                                    return std::strcmp(id, CLAP_EXT_PARAMS) ? nullptr : &kHostParams;
                                 },
                                 [](const clap_host_t *) {}, [](const clap_host_t *) {}, [](const clap_host_t *) {}};

// An output list that takes only `room` more events, like a host's bounded queue.
struct BoundedOut {
   jclap::core::TestEventList list;
   size_t room = 0;
   clap_output_events_t output() { return {this, push}; }
   static bool push(const clap_output_events_t *l, const clap_event_header_t *h) {
      auto *self = static_cast<BoundedOut *>(l->ctx);
      if (!self->room)
         return false;
      --self->room;
      self->list.add(*h);
      return true;
   }
};

struct Drained {
   int begins = 0, ends = 0, values = 0;
   double last = -1.0;
};

// Drains everything (up to `room` events) and counts what came out.
Drained drainAll(jclap::core::EditQueue &edits, size_t room = 100000) {
   BoundedOut out;
   out.room = room;
   const clap_output_events_t events = out.output();
   edits.drain(&events);
   Drained d;
   for (size_t i = 0; i < out.list.events.size(); ++i) {
      const clap_event_header_t *h = out.list.at(i);
      if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
         ++d.begins;
      else if (h->type == CLAP_EVENT_PARAM_GESTURE_END)
         ++d.ends;
      else if (h->type == CLAP_EVENT_PARAM_VALUE) {
         ++d.values;
         d.last = reinterpret_cast<const clap_event_param_value_t *>(h)->value;
      }
   }
   return d;
}

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

   // EditQueue, nothing draining for a long drag: the gesture still ends, on its last value.
   {
      jclap::core::EditQueue edits;
      edits.init(&kParamsHost);
      flushRequests = 0;
      edits.begin(kLevel);
      for (int i = 0; i < 2000; ++i)
         edits.value(kLevel, i);
      edits.end(kLevel);
      // One request until a drain, not one per edit.
      JCLAP_CHECK_EQ(flushRequests, 1);
      const Drained d = drainAll(edits);
      JCLAP_CHECK(d.begins == 1 && d.ends == 1);
      JCLAP_CHECK_EQ(d.last, 1999.0);
      JCLAP_CHECK(d.values > 1000);
      // After a drain, the next edit asks again.
      edits.set(kLevel, 1.0);
      JCLAP_CHECK_EQ(flushRequests, 2);
      JCLAP_CHECK_EQ(drainAll(edits).ends, 1);
   }
   // Many whole gestures while full: every Begin that goes out has its End.
   {
      jclap::core::EditQueue edits;
      edits.init(&kParamsHost);
      for (int i = 0; i < 1000; ++i)
         edits.set(kLevel, i);
      const Drained d = drainAll(edits);
      JCLAP_CHECK(d.begins > 0 && d.begins == d.ends);
   }
   // More open gestures than it tracks: the extra Begins are dropped along with their Ends.
   {
      jclap::core::EditQueue edits;
      edits.init(&kParamsHost);
      for (clap_id id = 0; id < 40; ++id)
         edits.begin(id);
      for (clap_id id = 0; id < 40; ++id)
         edits.end(id);
      const Drained d = drainAll(edits);
      JCLAP_CHECK(d.begins > 0 && d.begins == d.ends);
   }
   // An event the host refuses stays queued: the End comes out on the next drain.
   {
      jclap::core::EditQueue edits;
      edits.init(&kParamsHost);
      edits.set(kLevel, 4.0);
      const Drained first = drainAll(edits, 2);
      JCLAP_CHECK(first.begins == 1 && first.values == 1 && first.ends == 0);
      const Drained second = drainAll(edits);
      JCLAP_CHECK(second.begins == 0 && second.values == 0 && second.ends == 1);
   }

   // fillParamInfo takes a null name or module as empty.
   {
      clap_param_info_t info;
      jclap::core::fillParamInfo({kLevel, nullptr, nullptr, 0, 1, 0}, &info);
      JCLAP_CHECK(info.name[0] == 0 && info.module[0] == 0 && info.id == kLevel);
   }
   static_assert(!std::is_copy_constructible_v<jclap::core::TestHost>, "a copy would destroy the plugin twice");

   // SpscQueue: FIFO order, and full/empty are reported.
   jclap::core::SpscQueue<int, 4> queue;
   for (int i = 0; i < 4; ++i)
      JCLAP_CHECK(queue.push(i));
   JCLAP_CHECK(!queue.push(4));
   int item = -1;
   JCLAP_CHECK(queue.pop(item) && item == 0);
   JCLAP_CHECK(queue.push(4));
   for (int expect : {1, 2, 3, 4})
      JCLAP_CHECK(queue.pop(item) && item == expect);
   JCLAP_CHECK(!queue.pop(item));

   // SampleFifo: reads the newest samples, oldest first, across the wrap.
   jclap::core::SampleFifo<8> fifo;
   const float ramp[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
   fifo.write(ramp, 10);
   float last[3] = {};
   JCLAP_CHECK_EQ(fifo.read(last, 3), 10u);
   JCLAP_CHECK(last[0] == 7 && last[1] == 8 && last[2] == 9);

   // TripleBuffer: the reader sees only published structs, always the newest, and knows when
   // nothing new came.
   struct Pair {
      int a = 0, b = 0;
   };
   jclap::core::TripleBuffer<Pair> triple;
   JCLAP_CHECK(!triple.update());
   for (int i = 1; i <= 3; ++i) {
      triple.back() = {i, -i};
      triple.publish();
   }
   triple.back() = {99, 99}; // written, never published
   JCLAP_CHECK(triple.update());
   JCLAP_CHECK(triple.front().a == 3 && triple.front().b == -3);
   JCLAP_CHECK(!triple.update());
   triple.back() = {4, -4};
   triple.publish();
   JCLAP_CHECK(triple.update() && triple.front().a == 4);

   return jclap::core::testResult();
}
