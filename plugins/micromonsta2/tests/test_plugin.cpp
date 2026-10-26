// Drives the plugin the way a host does: param and modulation events in, MIDI CCs out, and checks
// the values the panel reads.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "cc_map.h"
#include "editor.h"

extern "C" const clap_plugin_entry_t clap_entry;

#define CHECK_EQ JCLAP_CHECK_EQ

namespace {

struct Cc {
   int channel, cc, value;
};

// The control changes in a block's output.
std::vector<Cc> ccs(const jclap::core::TestOutput &out) {
   std::vector<Cc> result;
   for (const auto &m : out.midi) {
      if ((m.status & 0xF0) == 0xB0)
         result.push_back({(m.status & 0x0F) + 1, m.data1, m.data2});
   }
   return result;
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

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
   // What the panel reads, through the same interface the editor uses.
   mm2::EditorHost &panel = *h.object<mm2::EditorHost>();

   // Nothing has been set yet, so nothing is sent.
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // Automation sets the knob: one CC out, and the panel shows it.
   h.setParam(74, 100);
   auto out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out[0].channel, 1);
   CHECK_EQ(out[0].cc, 74);
   CHECK_EQ(out[0].value, 100);
   CHECK_EQ(panel.paramValue(74), 100.0);
   CHECK_EQ(panel.paramModulated(74), 100.0);

   // A modulator pushes it up by 20: the synth gets 120, the panel shows 120 as the sent value, and
   // the knob's own setting (and the host's view of it) stays at 100.
   h.modulate(74, 20);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out[0].value, 120);
   CHECK_EQ(panel.paramModulated(74), 120.0);
   CHECK_EQ(panel.paramValue(74), 100.0);
   CHECK_EQ(h.paramValue(74), 100.0);

   // Modulation past the ends is clamped to the CC range.
   h.modulate(74, -150);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out[0].value, 0);
   CHECK_EQ(panel.paramModulated(74), 0.0);

   // A modulator on a knob nobody set still drives it, from its default.
   h.modulate(71, 10.4);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out[0].cc, 71);
   CHECK_EQ(out[0].value, 10);

   // Unchanged blocks send nothing.
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // Modulation steps closer than 5 ms on one CC are thinned for 5-pin MIDI: the first goes out at
   // once and the latest with the next block.
   h.modulate(74, 1);
   h.modulate(74, 2, 10);
   h.modulate(74, 3, 20);
   h.modulate(74, 4, 255);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 2u);
   CHECK_EQ(out.empty() ? 0 : out[0].value, 101);
   CHECK_EQ(out.size() < 2 ? 0 : out[1].value, 104);
   h.modulate(74, 5, 0);
   h.modulate(74, 2, 100);
   CHECK_EQ(ccs(h.process()).size(), 0u);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 1u);
   CHECK_EQ(out.empty() ? 0 : out[0].value, 102);

   // Panel edits reach the host as a gesture and go out as a CC on the next block.
   panel.beginEdit(73);
   panel.setParam(73, 90);
   panel.endEdit(73);
   const auto edit = h.process();
   CHECK_EQ(edit.gestures.size(), 2u);
   CHECK_EQ(edit.paramValues.size(), 1u);
   CHECK_EQ(ccs(edit).size(), 1u);
   CHECK_EQ(h.paramValue(73), 90.0);

   // The channel stepper is a param edit too, and moves later CCs to the new channel.
   panel.setChannel(3);
   const auto moved = ccs(h.process());
   CHECK_EQ(h.paramValue(mm2::kChannelParamId), 3.0);
   CHECK_EQ(moved.empty() ? 0 : moved[0].channel, 3);

   // A channel change mid-block moves the CCs after it at once: the set knobs go out again on the new
   // channel at that time, and the later edit with them (101 plus the +2 modulation still on 74).
   h.setParam(mm2::kChannelParamId, 5, 10);
   h.setParam(74, 101, 20);
   const auto midBlock = h.process();
   CHECK_EQ(midBlock.midi.empty() ? 0 : midBlock.midi.front().time, 10u);
   bool all5 = !midBlock.midi.empty(), got74 = false;
   for (const Cc &c : ccs(midBlock)) {
      all5 = all5 && c.channel == 5;
      got74 = got74 || (c.cc == 74 && c.value == 103);
   }
   JCLAP_CHECK(all5 && got74);
   CHECK_EQ(ccs(h.process()).size(), 0u);

   // A NaN from the host is ignored: nothing goes out, and the knob keeps its value.
   h.setParam(74, std::nan(""));
   h.modulate(73, std::nan(""));
   CHECK_EQ(ccs(h.process()).size(), 0u);
   CHECK_EQ(h.paramValue(74), 101.0);
   JCLAP_CHECK(std::isfinite(panel.paramModulated(73)));
   double parsed = 0.0;
   JCLAP_CHECK(!h.textToValue(74, "nan", parsed) && !h.textToValue(74, "inf", parsed));
   JCLAP_CHECK(h.textToValue(74, "12", parsed) && parsed == 12.0);

   // State: a round trip, and a NaN entry (from an older build) only loses that knob, which goes back
   // to its default and is not sent.
   {
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      Buffer saved;
      JCLAP_CHECK(state->save(h.plugin(), &saved.out));
      h.setParam(73, 10);
      h.process();
      JCLAP_CHECK(state->load(h.plugin(), &saved.in));
      CHECK_EQ(h.paramValue(73), 90.0);
      CHECK_EQ(h.paramValue(74), 101.0);
      CHECK_EQ(h.paramValue(mm2::kChannelParamId), 5.0);

      // Header (16 bytes), then {cc, touched, value} entries of 16 bytes.
      Buffer bad;
      bad.bytes = saved.bytes;
      for (size_t at = 16; at + 16 <= bad.bytes.size(); at += 16) {
         int32_t cc;
         std::memcpy(&cc, bad.bytes.data() + at, 4);
         if (cc == 73) {
            const double nan = std::nan("");
            std::memcpy(bad.bytes.data() + at + 8, &nan, 8);
         }
      }
      JCLAP_CHECK(state->load(h.plugin(), &bad.in));
      CHECK_EQ(h.paramValue(73), 0.0);
      CHECK_EQ(h.paramValue(74), 101.0);
      bool sent73 = false;
      for (const Cc &c : ccs(h.process()))
         sent73 = sent73 || c.cc == 73;
      JCLAP_CHECK(!sent73);
   }

   // #77: the defaults are an init patch, so SEND ALL on a fresh instance doesn't mute the synth.
   {
      jclap::core::TestHost f(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
      mm2::EditorHost &p = *f.object<mm2::EditorHost>();
      p.sendAll();
      int got[128];
      std::fill(std::begin(got), std::end(got), -1);
      for (const Cc &c : ccs(f.process()))
         got[c.cc] = c.value;
      CHECK_EQ(got[7], 100);
      CHECK_EQ(got[48], 127);
      CHECK_EQ(got[74], 127);
      CHECK_EQ(got[79], 127);
      CHECK_EQ(got[40], 64);
      CHECK_EQ(got[49], 0);
      JCLAP_CHECK(f.paramValue(7) == 100.0);
   }

   // A state saved on channel 3 goes out once when it loads, not again on the next block.
   {
      jclap::core::TestHost a(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
      a.setParam(mm2::kChannelParamId, 3);
      a.setParam(74, 60);
      a.process();
      auto *state = static_cast<const clap_plugin_state_t *>(a.plugin()->get_extension(a.plugin(), CLAP_EXT_STATE));
      Buffer saved;
      JCLAP_CHECK(state->save(a.plugin(), &saved.out));
      jclap::core::TestHost b(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
      auto *stateB = static_cast<const clap_plugin_state_t *>(b.plugin()->get_extension(b.plugin(), CLAP_EXT_STATE));
      JCLAP_CHECK(stateB->load(b.plugin(), &saved.in));
      const auto first = ccs(b.process());
      CHECK_EQ(first.size(), 1u);
      CHECK_EQ(first.empty() ? 0 : first[0].channel, 3);
      CHECK_EQ(ccs(b.process()).size(), 0u);

      // Modulating a knob nobody set sends it but doesn't change the saved state.
      Buffer before, after;
      JCLAP_CHECK(stateB->save(b.plugin(), &before.out));
      b.modulate(71, 10);
      CHECK_EQ(ccs(b.process()).size(), 1u);
      JCLAP_CHECK(stateB->save(b.plugin(), &after.out));
      JCLAP_CHECK(before.bytes == after.bytes);

      // reset() keeps the modulation: the next send of 71 still includes it.
      b.plugin()->reset(b.plugin());
      b.setParam(71, 20);
      const auto afterReset = ccs(b.process());
      CHECK_EQ(afterReset.empty() ? 0 : afterReset[0].value, 30);

      // A controller's CC passed through on the synth's channel counts as sent: the knob isn't
      // pushed back over it, and setting the knob to the value the synth already has sends nothing.
      b.midi(0xB2, 74, 90);
      const auto through = b.process();
      CHECK_EQ(ccs(through).size(), 1u);
      CHECK_EQ(ccs(b.process()).size(), 0u);
      b.setParam(74, 90);
      CHECK_EQ(ccs(b.process()).size(), 0u);
      b.setParam(74, 91);
      CHECK_EQ(ccs(b.process()).size(), 1u);

      // A note the host's queue refuses goes out on the next block, in order.
      b.setOutputLimit(1);
      b.midi(0x92, 60, 100);
      b.midi(0x82, 60, 0);
      CHECK_EQ(b.process().midi.size(), 1u);
      b.setOutputLimit(SIZE_MAX);
      const auto late = b.process();
      CHECK_EQ(late.midi.size(), 1u);
      CHECK_EQ(late.midi.empty() ? 0 : late.midi[0].status, 0x82);
      CHECK_EQ(b.process().midi.size(), 0u);
   }

   // Loading a program from the synth's bank: Bank Select and Program Change go out, the knobs move
   // to the program's values without being sent (the synth already has them), and the panel gets
   // the program's mod routing.
   {
      jclap::core::TestHost a(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
      mm2::EditorHost &p = *a.object<mm2::EditorHost>();
      a.setParam(mm2::kChannelParamId, 2);
      a.setParam(7, 90);
      a.process();

      mm2::HardwarePatch patch;
      patch.program = 112;
      patch.name = "OctaBass";
      patch.category = "Bass";
      patch.values = {{74, 40}, {71, 99}, {52, 80}, {7, 10}};
      patch.routes[0] = {18, 12}; // lfo1 -> cuto
      p.loadProgram(patch);
      const auto out = a.process();
      CHECK_EQ(out.midi.size(), 3u);
      if (out.midi.size() == 3) {
         CHECK_EQ(out.midi[0].status, 0xB1);
         CHECK_EQ(out.midi[0].data1, 0);
         CHECK_EQ(out.midi[1].data1, 32);
         CHECK_EQ(out.midi[1].data2, 1);
         CHECK_EQ(out.midi[2].status, 0xC1);
         CHECK_EQ(out.midi[2].data1, 11);
      }
      // The host hears about each knob that moved, as a gesture.
      CHECK_EQ(out.paramValues.size(), 3u);
      CHECK_EQ(a.paramValue(74), 40.0);
      CHECK_EQ(a.paramValue(71), 99.0);
      CHECK_EQ(a.paramValue(52), 80.0);
      // Volume isn't part of a program.
      CHECK_EQ(a.paramValue(7), 90.0);
      CHECK_EQ(p.program(), 112);
      JCLAP_CHECK(p.route(0) == (mm2::ModRoute{18, 12}));
      JCLAP_CHECK(p.presetName() == "P112 OctaBass");
      CHECK_EQ(ccs(a.process()).size(), 0u);

      // Moving a knob afterwards sends it as usual.
      p.beginEdit(74);
      p.setParam(74, 41);
      p.endEdit(74);
      const auto moved = ccs(a.process());
      CHECK_EQ(moved.size(), 1u);
      CHECK_EQ(moved.empty() ? 0 : moved[0].value, 41);

      // The program and its routing are saved; loading the project sends the Program Change first,
      // then the knobs that were set.
      auto *state = static_cast<const clap_plugin_state_t *>(a.plugin()->get_extension(a.plugin(), CLAP_EXT_STATE));
      Buffer saved;
      JCLAP_CHECK(state->save(a.plugin(), &saved.out));
      jclap::core::TestHost b(clap_entry, "com.jeffmhopkins.jclap.micromonsta2");
      mm2::EditorHost &pb = *b.object<mm2::EditorHost>();
      auto *stateB = static_cast<const clap_plugin_state_t *>(b.plugin()->get_extension(b.plugin(), CLAP_EXT_STATE));
      JCLAP_CHECK(stateB->load(b.plugin(), &saved.in));
      CHECK_EQ(pb.program(), 112);
      JCLAP_CHECK(pb.route(0) == (mm2::ModRoute{18, 12}));
      JCLAP_CHECK(pb.presetName() == "P112 OctaBass");
      const auto restored = b.process();
      JCLAP_CHECK(restored.midi.size() >= 3 && restored.midi[2].status == 0xC1);
      bool sent74 = false;
      for (const Cc &c : ccs(restored))
         sent74 = sent74 || (c.cc == 74 && c.value == 41);
      JCLAP_CHECK(sent74);
   }

   // The bank from the synth: every program has a unique slot, every sound CC, values in range and
   // routing within the synth's lists. Program 106 is the one the capture checked Program Change on.
   {
      const auto &bank = mm2::factoryBank();
      JCLAP_CHECK(bank.size() > 100);
      bool seen[mm2::kProgramCount + 1] = {};
      for (const mm2::HardwarePatch &p : bank) {
         JCLAP_CHECK(p.program >= 1 && p.program <= mm2::kProgramCount && !seen[p.program]);
         seen[std::clamp(p.program, 0, mm2::kProgramCount)] = true;
         CHECK_EQ(p.values.size(), 58u);
         for (auto [cc, v] : p.values)
            JCLAP_CHECK(mm2::indexOfCc(cc) >= 0 && v >= 0 && v <= 127);
         for (const mm2::ModRoute &r : p.routes)
            JCLAP_CHECK(r.source < mm2::kModSourceCount && r.destination < mm2::kModDestinationCount);
      }
      const mm2::HardwarePatch *octa = mm2::findFactoryPatch("P013 OctaBass");
      JCLAP_CHECK(octa && octa->category == "Bass");
      const mm2::HardwarePatch *p106 = mm2::findFactoryPatch("P106 Stretchr");
      JCLAP_CHECK(p106 && mm2::bankOf(p106->program) == 1 && mm2::programChangeOf(p106->program) == 5);
      CHECK_EQ(mm2::factoryPresets().size(), bank.size());
   }

   // Program numbers map onto banks of 100.
   CHECK_EQ(mm2::bankOf(1), 0);
   CHECK_EQ(mm2::programChangeOf(1), 0);
   CHECK_EQ(mm2::bankOf(100), 0);
   CHECK_EQ(mm2::bankOf(101), 1);
   CHECK_EQ(mm2::programChangeOf(300), 99);

   return jclap::core::testResult();
}
