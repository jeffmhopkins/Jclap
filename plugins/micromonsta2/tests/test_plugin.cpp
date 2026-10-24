// Drives the plugin the way a host does: param and modulation events in, MIDI CCs out, and checks
// the values the panel reads.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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

   // Several modulation steps in one block each go out.
   h.modulate(74, 1);
   h.modulate(74, 2);
   out = ccs(h.process());
   CHECK_EQ(out.size(), 2u);

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
         if (cc == 74) {
            const double nan = std::nan("");
            std::memcpy(bad.bytes.data() + at + 8, &nan, 8);
         }
      }
      JCLAP_CHECK(state->load(h.plugin(), &bad.in));
      CHECK_EQ(h.paramValue(74), 0.0);
      CHECK_EQ(h.paramValue(73), 90.0);
      bool sent74 = false;
      for (const Cc &c : ccs(h.process()))
         sent74 = sent74 || c.cc == 74;
      JCLAP_CHECK(!sent74);
   }

   return jclap::core::testResult();
}
