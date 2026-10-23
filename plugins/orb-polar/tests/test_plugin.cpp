// Drives the whole plugin the way a host does: XY CCs in, angle/radius CCs out.

#include <jclap/core/test_host.h>

extern "C" const clap_plugin_entry_t clap_entry;

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.polar-xy");

   // X hard right (CC 16 = 127) at the default Y of 64: full radius at angle 0.
   h.midi(0xB0, 16, 127);
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
   return jclap::core::testResult();
}
