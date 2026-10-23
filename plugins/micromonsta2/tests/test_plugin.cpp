// Drives the plugin the way a host does: param and modulation events in, MIDI CCs out, and checks
// the values the panel reads.

#include <clap/clap.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "cc_map.h"
#include "editor.h"

extern "C" const clap_plugin_entry_t clap_entry;

static int failures = 0;

#define CHECK_EQ(a, b)                                                                             \
   do {                                                                                            \
      const auto va = (a);                                                                         \
      const auto vb = (b);                                                                         \
      if (va != vb) {                                                                              \
         std::printf("%s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, (int)va, (int)vb); \
         ++failures;                                                                               \
      }                                                                                            \
   } while (0)

namespace {

const void *hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void hostNoop(const clap_host_t *) {}

const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "test", "jclap", "", "1", hostGetExtension,
                           hostNoop, hostNoop, hostNoop};

struct EventList {
   std::vector<std::vector<char>> events;

   void add(const clap_event_header_t &h) {
      const char *p = reinterpret_cast<const char *>(&h);
      events.emplace_back(p, p + h.size);
   }

   static uint32_t size(const clap_input_events_t *l) {
      return static_cast<uint32_t>(static_cast<EventList *>(l->ctx)->events.size());
   }
   static const clap_event_header_t *get(const clap_input_events_t *l, uint32_t i) {
      return reinterpret_cast<const clap_event_header_t *>(static_cast<EventList *>(l->ctx)->events[i].data());
   }
   static bool push(const clap_output_events_t *l, const clap_event_header_t *h) {
      static_cast<EventList *>(l->ctx)->add(*h);
      return true;
   }
};

struct Cc {
   int channel, cc, value;
};

class Harness {
public:
   Harness() {
      auto *factory =
         static_cast<const clap_plugin_factory_t *>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
      plugin_ = factory->create_plugin(factory, &kHost, "com.jeffmhopkins.jclap.micromonsta2");
      plugin_->init(plugin_);
      plugin_->activate(plugin_, 48000, 1, 256);
      plugin_->start_processing(plugin_);
      params_ = static_cast<const clap_plugin_params_t *>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
   }

   ~Harness() {
      plugin_->stop_processing(plugin_);
      plugin_->deactivate(plugin_);
      plugin_->destroy(plugin_);
   }

   // What the panel reads, through the same interface the editor uses.
   mm2::EditorHost &panel() { return *static_cast<mm2::EditorHost *>(plugin_->plugin_data); }

   void setValue(int cc, double value) {
      clap_event_param_value_t ev{};
      ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
      ev.param_id = static_cast<clap_id>(cc);
      ev.note_id = ev.port_index = ev.channel = ev.key = -1;
      ev.value = value;
      in_.add(ev.header);
   }

   // Bitwig sends monophonic modulator output as a param mod event with no note.
   void modulate(int cc, double amount) {
      clap_event_param_mod_t ev{};
      ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
      ev.param_id = static_cast<clap_id>(cc);
      ev.note_id = ev.port_index = ev.channel = ev.key = -1;
      ev.amount = amount;
      in_.add(ev.header);
   }

   double hostValue(int cc) {
      double v = -1;
      params_->get_value(plugin_, static_cast<clap_id>(cc), &v);
      return v;
   }

   // Runs one block and returns the CCs it sent.
   std::vector<Cc> process() {
      EventList out;
      const clap_input_events_t in = {&in_, EventList::size, EventList::get};
      const clap_output_events_t outEvents = {&out, EventList::push};
      clap_process_t proc{};
      proc.frames_count = 256;
      proc.steady_time = -1;
      proc.in_events = &in;
      proc.out_events = &outEvents;
      plugin_->process(plugin_, &proc);
      in_.events.clear();

      std::vector<Cc> ccs;
      for (auto &e : out.events) {
         auto *h = reinterpret_cast<const clap_event_header_t *>(e.data());
         if (h->type != CLAP_EVENT_MIDI)
            continue;
         auto *m = reinterpret_cast<const clap_event_midi_t *>(h);
         if ((m->data[0] & 0xF0) == 0xB0)
            ccs.push_back({(m->data[0] & 0x0F) + 1, m->data[1], m->data[2]});
      }
      return ccs;
   }

private:
   const clap_plugin_t *plugin_;
   const clap_plugin_params_t *params_;
   EventList in_;
};

} // namespace

int main() {
   Harness h;
   const int cutoff = mm2::indexOfCc(74);

   // Nothing has been set yet, so nothing is sent.
   CHECK_EQ(h.process().size(), 0u);

   // Automation sets the knob: one CC out, and the panel shows it.
   h.setValue(74, 100);
   auto ccs = h.process();
   CHECK_EQ(ccs.size(), 1u);
   CHECK_EQ(ccs[0].channel, 1);
   CHECK_EQ(ccs[0].cc, 74);
   CHECK_EQ(ccs[0].value, 100);
   CHECK_EQ(h.panel().baseValue(cutoff), 100.0);
   CHECK_EQ(h.panel().modulatedValue(cutoff), 100.0);

   // A modulator pushes it up by 20: the synth gets 120, the panel shows 120 as the sent value, and
   // the knob's own setting (and the host's view of it) stays at 100.
   h.modulate(74, 20);
   ccs = h.process();
   CHECK_EQ(ccs.size(), 1u);
   CHECK_EQ(ccs[0].value, 120);
   CHECK_EQ(h.panel().modulatedValue(cutoff), 120.0);
   CHECK_EQ(h.panel().baseValue(cutoff), 100.0);
   CHECK_EQ(h.hostValue(74), 100.0);

   // Modulation past the ends is clamped to the CC range.
   h.modulate(74, -150);
   ccs = h.process();
   CHECK_EQ(ccs.size(), 1u);
   CHECK_EQ(ccs[0].value, 0);
   CHECK_EQ(h.panel().modulatedValue(cutoff), 0.0);

   // A modulator on a knob nobody set still drives it, from its default.
   h.modulate(71, 10.4);
   ccs = h.process();
   CHECK_EQ(ccs.size(), 1u);
   CHECK_EQ(ccs[0].cc, 71);
   CHECK_EQ(ccs[0].value, 10);

   // Unchanged blocks send nothing.
   CHECK_EQ(h.process().size(), 0u);

   // Several modulation steps in one block each go out.
   h.modulate(74, 1);
   h.modulate(74, 2);
   ccs = h.process();
   CHECK_EQ(ccs.size(), 2u);

   if (failures)
      std::printf("%d failure(s)\n", failures);
   return failures ? 1 : 0;
}
