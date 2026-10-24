// Drives the whole plugin the way a host does: XY CCs in, angle/radius CCs out.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");

   // Nothing goes out until both axes are known.
   h.midi(0xB0, 16, 127);
   JCLAP_CHECK_EQ(h.process().midi.size(), 0u);
   // X hard right (CC 16 = 127) with Y centred: full radius at angle 0.
   h.midi(0xB0, 17, 64);
   auto out = h.process();
   JCLAP_CHECK_EQ(out.midi.size(), 2u);
   if (out.midi.size() == 2) {
      JCLAP_CHECK_EQ(out.midi[0].data1, 18); // angle out
      JCLAP_CHECK_EQ(out.midi[1].data1, 19); // radius out
      JCLAP_CHECK_EQ(out.midi[1].data2, 127);
   }

   // Other CCs and notes pass straight through.
   h.midi(0xB0, 74, 10);
   h.midi(0x90, 60, 100);
   JCLAP_CHECK_EQ(h.process().midi.size(), 2u);

   // Pass Input CCs on: the XY CC itself goes out too.
   h.setParam(7, 1.0);
   h.midi(0xB0, 17, 127);
   out = h.process();
   JCLAP_CHECK(!out.midi.empty() && out.midi[0].data1 == 17);

   JCLAP_CHECK(h.paramText(4, 90.0) == "90.0 deg");
   JCLAP_CHECK(h.paramText(4, 359.96) == "0.0 deg");
   double v = 0.0;
   JCLAP_CHECK(h.textToValue(0, "cc 20", v) && v == 20.0);
   JCLAP_CHECK(h.textToValue(0, " CC 21", v) && v == 21.0);
   JCLAP_CHECK(h.textToValue(4, "-90", v) && v == 270.0);
   JCLAP_CHECK(h.textToValue(4, "720", v) && v == 0.0);
   h.setParam(7, 0.0);
   h.process();

   // X and Y at the same time are one move: only the final point goes out, no point made of the new
   // X and the old Y.
   h.midi(0xB0, 16, 0);
   h.midi(0xB0, 17, 0);
   out = h.process();
   int angles = 0;
   for (const auto &m : out.midi)
      angles += m.data1 == 18;
   JCLAP_CHECK_EQ(angles, 1);

   // Channels are separate: a move on channel 2 doesn't pair with channel 1's position.
   h.midi(0xB1, 16, 127);
   JCLAP_CHECK_EQ(h.process().midi.size(), 0u);
   h.midi(0xB1, 17, 64);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 2 && (out.midi[0].status & 0x0F) == 1);

   // Unchanged values aren't sent again, and reset forgets what was sent without forgetting the
   // position: the next move resends both.
   h.midi(0xB1, 17, 64);
   JCLAP_CHECK_EQ(h.process().midi.size(), 0u);
   h.plugin()->reset(h.plugin());
   h.midi(0xB1, 17, 64);
   JCLAP_CHECK_EQ(h.process().midi.size(), 2u);

   // A setting change resends at once: the offset moves the angle, new output CCs get both.
   h.setParam(4, 90.0);
   out = h.process();
   // Channel 2 sends its new angle; channel 1 both, as the reset forgot what it had sent.
   JCLAP_CHECK_EQ(out.midi.size(), 3u);
   h.setParam(3, 30.0);
   out = h.process();
   JCLAP_CHECK_EQ(out.midi.size(), 4u);
   h.setParam(3, 19.0);
   h.setParam(4, 0.0);
   h.process();

   // A new X In CC doesn't pair the old CC's X with the next Y.
   h.setParam(0, 20.0);
   h.process();
   h.midi(0xB0, 17, 100);
   JCLAP_CHECK_EQ(h.process().midi.size(), 0u);
   h.midi(0xB0, 20, 64);
   JCLAP_CHECK_EQ(h.process().midi.size(), 2u);

   // Two inputs on one CC convert nothing and pass the CC through.
   h.setParam(1, 20.0);
   h.midi(0xB0, 20, 10);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 20);
   h.setParam(1, 17.0);
   h.process();

   // State round trip.
   {
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      std::vector<uint8_t> bytes;
      clap_ostream_t os{&bytes, [](const clap_ostream_t *s, const void *d, uint64_t n) -> int64_t {
                           auto *b = static_cast<std::vector<uint8_t> *>(s->ctx);
                           b->insert(b->end(), static_cast<const uint8_t *>(d), static_cast<const uint8_t *>(d) + n);
                           return static_cast<int64_t>(n);
                        }};
      JCLAP_CHECK(state->save(h.plugin(), &os));
      jclap::core::TestHost h2(clap_entry, "com.jeffmhopkins.jclap.polar-xy");
      auto *state2 = static_cast<const clap_plugin_state_t *>(h2.plugin()->get_extension(h2.plugin(), CLAP_EXT_STATE));
      size_t pos = 0;
      std::pair<std::vector<uint8_t> *, size_t *> ctx{&bytes, &pos};
      clap_istream_t is{&ctx, [](const clap_istream_t *s, void *d, uint64_t n) -> int64_t {
                           auto *c = static_cast<std::pair<std::vector<uint8_t> *, size_t *> *>(s->ctx);
                           const size_t k = std::min<size_t>(n, c->first->size() - *c->second);
                           std::memcpy(d, c->first->data() + *c->second, k);
                           *c->second += k;
                           return static_cast<int64_t>(k);
                        }};
      JCLAP_CHECK(state2->load(h2.plugin(), &is));
      JCLAP_CHECK(h2.paramValue(0) == 20.0 && h2.paramValue(1) == 17.0);
   }
   return jclap::core::testResult();
}
