// Drives the whole plugin the way a host does: XY CCs in, angle/radius CCs out.

#include <jclap/core/test_host.h>

#include "editor.h"
#include "polar.h"
#include "presets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

namespace {

// The angle and radius CCs a run of blocks sends on channel 1, in order.
struct Path {
   std::vector<int> angles, radii;
   bool sorted = true; // each block's events in time order
};

Path run(jclap::core::TestHost &h, int blocks) {
   Path p;
   for (int b = 0; b < blocks; ++b) {
      const auto out = h.process();
      uint32_t last = 0;
      for (const auto &m : out.midi) {
         p.sorted = p.sorted && m.time >= last && m.time < 256;
         last = m.time;
         if (m.data1 == 18)
            p.angles.push_back(m.data2);
         if (m.data1 == 19)
            p.radii.push_back(m.data2);
      }
   }
   return p;
}

void motionTests() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");
   // 100 ms of smoothing. The first position is jumped to: there is nowhere to glide from.
   h.setParam(8, 100.0);
   h.midi(0xB0, 16, 127);
   h.midi(0xB0, 17, 64);
   Path p = run(h, 1);
   JCLAP_CHECK(p.angles.size() == 1 && p.angles[0] == 0 && p.radii.size() == 1 && p.radii[0] == 127);

   // Straight across to the left edge: the radius glides down through the centre and back out, in
   // many small steps over about 100 ms (19 blocks), and ends exactly on the target.
   h.midi(0xB0, 16, 0);
   p = run(h, 40);
   JCLAP_CHECK(p.sorted);
   JCLAP_CHECK(p.radii.size() > 40);
   JCLAP_CHECK(!p.radii.empty() && *std::min_element(p.radii.begin(), p.radii.end()) == 0);
   JCLAP_CHECK(!p.radii.empty() && p.radii.back() == 127);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 64);
   JCLAP_CHECK(run(h, 5).radii.empty()); // settled: nothing more is sent

   // Around the circle: back to the right edge keeps the full radius and sweeps the angle the short
   // way (here either way is half a turn) instead of cutting through the middle.
   h.setParam(10, 1.0);
   h.midi(0xB0, 16, 127);
   p = run(h, 40);
   JCLAP_CHECK(p.sorted);
   JCLAP_CHECK(p.radii.empty() || *std::min_element(p.radii.begin(), p.radii.end()) >= 125);
   JCLAP_CHECK(p.angles.size() > 20);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 0);

   // Inertia: a quarter turn up swings past the top and comes back to settle there (60% swings
   // for a couple of seconds, so this runs 8 s).
   h.setParam(8, 0.0);
   h.setParam(9, 60.0);
   h.midi(0xB0, 16, 64);
   h.midi(0xB0, 17, 127);
   p = run(h, 1500);
   JCLAP_CHECK(!p.angles.empty() && *std::max_element(p.angles.begin(), p.angles.end()) > 36);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 32);
   JCLAP_CHECK(p.radii.empty() || p.radii.back() == 127);

   // A whirl: a turn and a half round the circle in quarter steps, about 150 ms apart. Around unwraps the
   // angle, so the output keeps going round the same way, through the bottom, and on past the top.
   std::vector<int> whirl;
   const int steps[][2] = {{0, 64}, {64, 0}, {127, 64}, {64, 127}, {0, 64}, {64, 0}};
   for (const auto &xy : steps) {
      h.midi(0xB0, 16, static_cast<uint8_t>(xy[0]));
      h.midi(0xB0, 17, static_cast<uint8_t>(xy[1]));
      p = run(h, 28);
      whirl.insert(whirl.end(), p.angles.begin(), p.angles.end());
   }
   // Counter-clockwise all the way: each angle is the last plus a small step (mod 127), never a
   // jump back.
   int backwards = 0;
   for (size_t i = 1; i < whirl.size(); ++i)
      backwards += ((whirl[i] - whirl[i - 1] + 127) % 127) > 63;
   JCLAP_CHECK(whirl.size() > 30);
   JCLAP_CHECK(backwards <= 2);

   // Turning both off mid-glide lands at once.
   h.setParam(9, 100.0);
   h.midi(0xB0, 16, 127);
   h.midi(0xB0, 17, 64);
   run(h, 2);
   h.setParam(9, 0.0);
   p = run(h, 1);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 0);
   JCLAP_CHECK(run(h, 2).angles.empty());
}

// The panel's XY pad: dragging it moves the shown channel like the controller, and holds that
// channel against the controller's X/Y CCs until released.
void dragTests() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");
   auto *editor = h.object<polar_xy::EditorHost>();

   // Before any CC, the pad drives channel 1, and both axes count as arrived.
   editor->dragInput(127, 64);
   auto out = h.process();
   JCLAP_CHECK(out.midi.size() == 2 && out.midi[0].status == 0xB0 && out.midi[0].data1 == 18 &&
               out.midi[0].data2 == 0 && out.midi[1].data1 == 19 && out.midi[1].data2 == 127);
   int x = 0, y = 0, channel = -1;
   editor->lastInput(x, y, channel);
   JCLAP_CHECK(x == 127 && y == 64 && channel == 0);

   // Held: the controller's X/Y on that channel are swallowed and change nothing.
   h.midi(0xB0, 16, 0);
   JCLAP_CHECK_EQ(h.process().midi.size(), 0u);
   editor->lastInput(x, y, channel);
   JCLAP_CHECK(x == 127);
   // Other channels still convert, but the panel stays on the dragged one.
   h.midi(0xB1, 16, 0);
   h.midi(0xB1, 17, 64);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 2 && (out.midi[0].status & 0x0F) == 1);
   editor->lastInput(x, y, channel);
   JCLAP_CHECK(channel == 0);

   // Released: the controller takes over with its next move.
   editor->releaseInput();
   h.midi(0xB0, 16, 64);
   h.midi(0xB0, 17, 127);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 18 && out.midi[0].data2 == 32);

   // A move released before the next block still lands (a double-click reset is set and let go at
   // once), gliding there briefly like every pad move.
   editor->dragInput(0, 64);
   editor->releaseInput();
   Path p = run(h, 40);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 64);
   JCLAP_CHECK(run(h, 2).angles.empty());

   // Pad moves glide even with Smoothing off: the mouse comes a few dozen times a second, a block at
   // a time, and stepping straight to each move is choppy. A quarter turn sweeps the angle through
   // the values between, one CC step at a time.
   editor->dragInput(64, 127);
   p = run(h, 40);
   JCLAP_CHECK(p.angles.size() > 10);
   int jumps = 0;
   for (size_t i = 1; i < p.angles.size(); ++i)
      jumps += std::abs(p.angles[i] - p.angles[i - 1]) > 2;
   JCLAP_CHECK_EQ(jumps, 0);
   JCLAP_CHECK(!p.angles.empty() && p.angles.back() == 32);

   // The pad isn't rounded to CC steps: circling it slowly at a small radius, where the CC grid has
   // only a few angles, each position still settles on its own angle, not the nearest grid point's.
   int angle = -1, wrong = 0;
   for (int i = 0; i <= 40; ++i) {
      const double a = i * jclap::kPi / 80.0;
      editor->dragInput(64.0 + 12.0 * std::cos(a), 64.0 + 12.0 * std::sin(a));
      p = run(h, 40);
      if (!p.angles.empty())
         angle = p.angles.back();
      wrong += angle != static_cast<int>(std::lround(a / (2.0 * jclap::kPi) * 127.0));
   }
   JCLAP_CHECK_EQ(wrong, 0);
   editor->releaseInput();

   // Smoothing applies to the pad too: it glides there in steps.
   h.setParam(8, 100.0);
   h.process();
   editor->dragInput(127, 64);
   p = run(h, 40);
   JCLAP_CHECK(p.radii.size() > 10);
   JCLAP_CHECK(!p.radii.empty() && p.radii.back() == 127);
   editor->releaseInput();

   // Pass Input CCs sends the pad's X/Y on too.
   h.setParam(8, 0.0);
   h.setParam(7, 1.0);
   h.process();
   editor->dragInput(64, 0);
   out = h.process();
   JCLAP_CHECK(out.midi.size() >= 2 && out.midi[0].data1 == 16 && out.midi[0].data2 == 64 &&
               out.midi[1].data1 == 17 && out.midi[1].data2 == 0);
   editor->releaseInput();
}

// The panel's SEND buttons: one output CC alone, for a MIDI learn.
void learnTests() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");
   auto *editor = h.object<polar_xy::EditorHost>();
   // Before anything was sent: 0.
   editor->sendOutput(1);
   auto out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 19 && out.midi[0].data2 == 0);

   // After: the value last sent, on that CC only.
   h.midi(0xB0, 16, 64);
   h.midi(0xB0, 17, 127);
   h.process();
   editor->sendOutput(0);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 18 && out.midi[0].data2 == 32);
   editor->sendOutput(1);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 19 && out.midi[0].data2 == 127);
   JCLAP_CHECK(h.process().midi.empty());

   // At 14 bits, only the MSB: the CC to learn.
   h.setParam(11, 1.0);
   h.process();
   editor->sendOutput(0);
   out = h.process();
   JCLAP_CHECK(out.midi.size() == 1 && out.midi[0].data1 == 18 && out.midi[0].data2 == 32);
}

// 14-bit output: each value goes out as its MSB on the output CC, then its LSB on that CC + 32.
void fineTests() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");
   h.setParam(11, 1.0);
   h.midi(0xB0, 16, 96);
   h.midi(0xB0, 17, 64);
   auto out = h.process();
   const jclap::PolarCC want = jclap::toPolar(jclap::ccToBipolar(96), 0.0, {}, jclap::kSteps14);
   JCLAP_CHECK(want.radius > 0 && (want.radius & 0x7F) != 0); // an LSB that shows
   JCLAP_CHECK(out.midi.size() == 4);
   if (out.midi.size() == 4) {
      JCLAP_CHECK(out.midi[0].data1 == 18 && out.midi[0].data2 == 0 && out.midi[1].data1 == 50 &&
                  out.midi[1].data2 == 0);
      JCLAP_CHECK(out.midi[2].data1 == 19 && out.midi[2].data2 == (want.radius >> 7) && out.midi[3].data1 == 51 &&
                  out.midi[3].data2 == (want.radius & 0x7F));
   }
   // The panel shows it at 7 bits.
   int angle = -1, radius = -1;
   JCLAP_CHECK(h.object<polar_xy::EditorHost>()->lastOutput(angle, radius) && angle == 0 &&
               radius == (want.radius >> 7));

   // 45 degrees, on the 14-bit circle: 16383 / 8 rounds to 2048, MSB 16 and LSB 0.
   h.midi(0xB0, 16, 127);
   h.midi(0xB0, 17, 127);
   out = h.process();
   JCLAP_CHECK(out.midi.size() >= 2 && out.midi[0].data1 == 18 && out.midi[0].data2 == 16 &&
               out.midi[1].data1 == 50 && out.midi[1].data2 == 0);

   // Gliding at 14 bits moves in far finer steps: a slow quarter turn sends many more angles than
   // the 32 a 7-bit CC has.
   h.setParam(10, 1.0);
   h.setParam(8, 1000.0);
   h.midi(0xB0, 16, 0);
   h.midi(0xB0, 17, 127);
   int lsbs = 0;
   for (int b = 0; b < 400; ++b)
      for (const auto &m : h.process().midi)
         lsbs += m.data1 == 50;
   JCLAP_CHECK(lsbs > 200);
   h.setParam(8, 0.0);
   h.setParam(10, 0.0);
   h.process();

   // Output CCs above 31 have no LSB CC to pair with, so the output goes back to 7-bit.
   h.setParam(3, 40.0);
   h.midi(0xB0, 16, 127);
   h.midi(0xB0, 17, 64);
   out = h.process();
   // (The new CC sends the old position first, then the move.)
   for (const auto &m : out.midi)
      JCLAP_CHECK(m.data1 == 18 || m.data1 == 40);
   JCLAP_CHECK(!out.midi.empty() && out.midi.back().data1 == 18 && out.midi.back().data2 == 0);

   JCLAP_CHECK(h.paramText(11, 1.0) == "14-bit");
}

} // namespace

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

   // State round trip, with the preset name.
   {
      JCLAP_CHECK(h.object<polar_xy::EditorHost>()->presetName() == "Init");
      h.object<polar_xy::EditorHost>()->setPresetName("Whirlpool");
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
      JCLAP_CHECK(h2.object<polar_xy::EditorHost>()->presetName() == "Whirlpool");

      // Version 1 state, from before Smoothing, Inertia, Motion Path and Output Resolution: those load
      // as defaults.
      h2.setParam(8, 500.0);
      h2.setParam(11, 1.0);
      h2.process();
      const char magic[4] = {'J', 'C', 'O', 'P'};
      const uint32_t version = 1;
      const double v1[8] = {21, 22, 23, 24, 45, 1, 1, 0};
      bytes.assign(magic, magic + 4);
      bytes.insert(bytes.end(), reinterpret_cast<const uint8_t *>(&version), reinterpret_cast<const uint8_t *>(&version) + 4);
      bytes.insert(bytes.end(), reinterpret_cast<const uint8_t *>(v1), reinterpret_cast<const uint8_t *>(v1) + sizeof(v1));
      pos = 0;
      JCLAP_CHECK(state2->load(h2.plugin(), &is));
      JCLAP_CHECK(h2.paramValue(0) == 21.0 && h2.paramValue(4) == 45.0 && h2.paramValue(6) == 1.0);
      JCLAP_CHECK(h2.paramValue(8) == 0.0 && h2.paramValue(9) == 0.0 && h2.paramValue(10) == 0.0 &&
                  h2.paramValue(11) == 0.0);
      JCLAP_CHECK(h2.object<polar_xy::EditorHost>()->presetName().empty()); // no name before version 3
   }

   JCLAP_CHECK(h.paramText(8, 250.0) == "250 ms");
   JCLAP_CHECK(h.paramText(8, 1500.0) == "1.50 s");
   JCLAP_CHECK(h.paramText(9, 40.0) == "40%");
   JCLAP_CHECK(h.paramText(10, 1.0) == "Around");
   JCLAP_CHECK(h.textToValue(8, "1.5 s", v) && v == 1500.0);
   JCLAP_CHECK(h.textToValue(8, "80ms", v) && v == 80.0);
   JCLAP_CHECK(h.textToValue(10, "around", v) && v == 1.0);

   // The factory bank is one Init with every param at its default.
   const auto &factory = polar_xy::factoryPresets();
   JCLAP_CHECK(factory.size() == 1 && factory[0].name == "Init" && factory[0].values.size() == polar_xy::kParamCount);
   if (!factory.empty())
      for (const auto &[id, value] : factory[0].values)
         JCLAP_CHECK(value == polar_xy::kParams[id].def);

   motionTests();
   dragTests();
   fineTests();
   learnTests();
   return jclap::core::testResult();
}
