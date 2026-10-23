// Drives the plugin the way a host does. Start your own tests from these.

#include <jclap/core/test_host.h>

#include <cmath>

#include "editor.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace plugin_template;

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.plugin-template");

   float inL[4] = {0.5f, 0.5f, 0.5f, 0.5f}, inR[4] = {0.5f, 0.5f, 0.5f, 0.5f}, outL[4], outR[4];
   float *in[2] = {inL, inR}, *out[2] = {outL, outR};

   // Defaults: 0 dB, centre pan, so audio passes unchanged.
   h.process(4, in, out);
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f) < 1e-4f && std::fabs(outR[0] - 0.5f) < 1e-4f);

   // -6 dB halves it (near enough); hard left silences the right.
   h.setParam(kGain, -6.0206);
   h.setParam(kPan, -1.0);
   h.process(4, in, out);
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f * std::sqrt(2.0f) * 0.5f) < 1e-3f);
   JCLAP_CHECK(std::fabs(outR[0]) < 1e-6f);

   // Mute.
   h.setParam(kMute, 1.0);
   h.process(4, in, out);
   JCLAP_CHECK_EQ(outL[0], 0.0f);

   // Panel edits reach the host as a gesture.
   EditorHost &panel = *h.object<EditorHost>();
   panel.beginEdit(kGain);
   panel.setParam(kGain, 3.0);
   panel.endEdit(kGain);
   const auto edits = h.process(4, in, out);
   JCLAP_CHECK_EQ(edits.gestures.size(), 2u);
   JCLAP_CHECK_EQ(h.paramValue(kGain), 3.0);
   JCLAP_CHECK(h.paramText(kPan, -0.5) == "50L");

   return jclap::core::testResult();
}
