// Drives the plugin the way a host does: param events and the synth's own CCs in, MIDI and param
// changes out, and checks the values the panel reads.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cc_map.h"
#include "editor.h"

extern "C" const clap_plugin_entry_t clap_entry;

#define CHECK_EQ JCLAP_CHECK_EQ

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.matrixbrute";

struct Cc {
   int channel, cc, value;
};

// The control changes in a block's output.
std::vector<Cc> ccs(const jclap::core::TestOutput &out) {
   std::vector<Cc> result;
   for (const auto &m : out.midi)
      if ((m.status & 0xF0) == 0xB0)
         result.push_back({(m.status & 0x0F) + 1, m.data1, m.data2});
   return result;
}

int gestures(const jclap::core::TestOutput &out, uint16_t type) {
   return static_cast<int>(std::count_if(out.gestures.begin(), out.gestures.end(),
                                         [&](const clap_event_param_gesture_t &g) { return g.header.type == type; }));
}

// A byte buffer the plugin's state extension writes to and reads from.
struct Buffer {
   std::vector<uint8_t> bytes;
   size_t pos = 0;
   clap_ostream_t out{this, [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
                         auto *b = static_cast<Buffer *>(s->ctx);
                         b->bytes.insert(b->bytes.end(), static_cast<const uint8_t *>(data),
                                         static_cast<const uint8_t *>(data) + size);
                         return static_cast<int64_t>(size);
                      }};
   clap_istream_t in{this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
                        auto *b = static_cast<Buffer *>(s->ctx);
                        const size_t n = std::min<size_t>(size, b->bytes.size() - b->pos);
                        std::memcpy(data, b->bytes.data() + b->pos, n);
                        b->pos += n;
                        return static_cast<int64_t>(n);
                     }};
};

const clap_plugin_state_t *stateOf(jclap::core::TestHost &h) {
   return static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
}

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, kId);
   mb::EditorHost &panel = *h.object<mb::EditorHost>();

   // Nothing has been set yet, so nothing is sent, and no knob is known.
   CHECK_EQ(ccs(h.process()).size(), 0u);
   CHECK_EQ(panel.known(23), false);

   // Automation sets Steiner cutoff: one 7-bit CC out.
   h.setParam(23, 100.4);
   auto out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out.empty() ? 0 : out[0].cc, 23);
   CHECK_EQ(out.empty() ? 0 : out[0].value, 100);
   CHECK_EQ(panel.known(23), true);
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // With 14-bit on, every set knob goes again: a 14-bit one as coarse then fine (on CC + 32).
   h.setParam(mb::kFineParamId, 1);
   h.setParam(83, 30); // resonance has no fine byte
   out = ccs(h.process());
   CHECK_EQ(out.size(), 3u);
   if (out.size() == 3) {
      CHECK_EQ(out[0].cc, 23);
      CHECK_EQ(out[0].value, 100);
      CHECK_EQ(out[1].cc, 55);
      CHECK_EQ(out[1].value, 51); // 0.4 of 128
      CHECK_EQ(out[2].cc, 83);
      CHECK_EQ(out[2].value, 30);
   }
   JCLAP_CHECK(h.paramText(23, 100.4) == "100.40");
   JCLAP_CHECK(h.paramText(83, 30) == "30");

   // The synth's own ladder cutoff knob comes in: it passes through to the synth (for Local off), the
   // knob follows, the host hears a gesture it can record, and nothing extra goes back out.
   h.midi(0xB0, 25, 90, 10);
   auto block = h.process();
   out = ccs(block);
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out.empty() ? 0 : out[0].value, 90);
   CHECK_EQ(block.paramValues.size(), 1u);
   CHECK_EQ(block.paramValues.empty() ? 0u : block.paramValues[0].param_id, 25u);
   CHECK_EQ(block.paramValues.empty() ? 0.0 : block.paramValues[0].value, 90.0);
   CHECK_EQ(gestures(block, CLAP_EVENT_PARAM_GESTURE_BEGIN), 1);
   CHECK_EQ(panel.paramValue(25), 90.0);
   CHECK_EQ(panel.known(25), true);
   CHECK_EQ(panel.heardCount(), 1u);

   // Its fine byte refines it, in the same gesture.
   h.midi(0xB0, 57, 64);
   block = h.process();
   CHECK_EQ(ccs(block).size(), 1u);
   CHECK_EQ(panel.paramValue(25), 90.5);
   CHECK_EQ(gestures(block, CLAP_EVENT_PARAM_GESTURE_BEGIN), 0);
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // Once the knob has been still for 0.3 s the gesture ends, once.
   int ends = 0;
   for (int i = 0; i < 70; ++i)
      ends += gestures(h.process(), CLAP_EVENT_PARAM_GESTURE_END);
   CHECK_EQ(ends, 1);

   // Another channel's CCs pass through without moving anything.
   h.midi(0xB3, 25, 10);
   block = h.process();
   CHECK_EQ(ccs(block).size(), 1u);
   CHECK_EQ(block.paramValues.size(), 0u);
   CHECK_EQ(panel.paramValue(25), 90.5);

   // With Follow off a CC still passes through, and the knob doesn't push its own value back over it.
   h.setParam(mb::kFollowParamId, 0);
   h.process();
   h.midi(0xB0, 23, 5);
   block = h.process();
   CHECK_EQ(ccs(block).size(), 1u);
   CHECK_EQ(block.paramValues.size(), 0u);
   CHECK_EQ(panel.paramValue(23), 100.4);
   CHECK_EQ(ccs(h.process()).size(), 0u);
   h.setParam(mb::kFollowParamId, 1);
   h.process();

   // A Program Change from the synth: it loaded another preset, so no knob is known and none is sent.
   h.midi(0xC0, 12, 0);
   h.process();
   CHECK_EQ(panel.heardProgram(), 12);
   CHECK_EQ(panel.known(23), false);
   CHECK_EQ(panel.known(25), false);
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // Moving a knob afterwards sends it again.
   h.setParam(23, 64);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 2u);
   CHECK_EQ(out.empty() ? 0 : out[0].value, 64);

   // A saved project restores the settings and sends only the knobs that were known.
   {
      Buffer saved;
      JCLAP_CHECK(stateOf(h)->save(h.plugin(), &saved.out));
      jclap::core::TestHost b(clap_entry, kId);
      JCLAP_CHECK(stateOf(b)->load(b.plugin(), &saved.in));
      out = ccs(b.process());
      CHECK_EQ(out.size(), 2u); // cutoff, coarse and fine
      CHECK_EQ(b.paramValue(mb::kFineParamId), 1.0);
      CHECK_EQ(b.paramValue(23), 64.0);
      CHECK_EQ(b.paramValue(25), 90.5);
      CHECK_EQ(b.object<mb::EditorHost>()->known(25), false);

      Buffer bad;
      bad.bytes = {'J', 'M', 'M', '2'};
      JCLAP_CHECK(!stateOf(b)->load(b.plugin(), &bad.in));
   }

   // Notes pass straight through.
   h.midi(0x90, 60, 100);
   block = h.process();
   CHECK_EQ(block.midi.size(), 1u);
   CHECK_EQ(block.midi.empty() ? 0 : block.midi[0].status, 0x90);

   return jclap::core::testFailures() ? 1 : 0;
}
