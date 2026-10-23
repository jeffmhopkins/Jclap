// Drives the plugin the way a host does: param and modulation events in, MIDI CCs out, and checks
// the values the panel reads.

#include <jclap/core/test_host.h>

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

   return jclap::core::testResult();
}
