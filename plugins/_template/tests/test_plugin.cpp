// Drives the plugin the way a host does. Start your own tests from these.

#include <jclap/core/test_host.h>

#include <algorithm>
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

   // Swap routing at full width moves the left input to the right output.
   h.setParam(kPan, 0.0);
   h.setParam(kRouting, 1.0);
   inR[0] = 0.0f;
   h.process(4, in, out);
   JCLAP_CHECK(std::fabs(outL[0]) < 1e-4f && outR[0] > 0.1f);
   // Width 0 folds to mono.
   h.setParam(kWidth, 0.0);
   h.process(4, in, out);
   JCLAP_CHECK(std::fabs(outL[0] - outR[0]) < 1e-6f);
   JCLAP_CHECK(h.paramText(kRouting, 3.0) == "Right only");
   JCLAP_CHECK(h.paramText(kWidth, 1.5) == "150%");

   // Host modulation adds to a param without changing it: +6 dB on top of -6 dB is unity.
   h.setParam(kMute, 0.0);
   h.setParam(kWidth, 1.0);
   h.setParam(kRouting, 0.0);
   h.setParam(kGain, -6.0206);
   h.modulate(kGain, 6.0206);
   inR[0] = 0.5f;
   h.process(4, in, out);
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f) < 1e-3f);
   JCLAP_CHECK_EQ(h.paramValue(kGain), -6.0206);
   JCLAP_CHECK(std::fabs(h.object<EditorHost>()->paramModulated(kGain)) < 1e-9);
   h.modulate(kGain, 0.0);
   h.setParam(kWidth, 0.0);

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

   // The test tone: a note from the panel's keyboard sounds, lights its key and bumps the count.
   h.setParam(kMute, 0.0);
   h.setParam(kGain, 0.0);
   h.setParam(kRouting, 0.0);
   h.setParam(kWidth, 1.0);
   inL[0] = inL[1] = inL[2] = inL[3] = 0.0f;
   inR[0] = inR[1] = inR[2] = inR[3] = 0.0f;
   float bigL[256] = {}, bigR[256] = {}, outBigL[256], outBigR[256];
   float *bigIn[2] = {bigL, bigR}, *bigOut[2] = {outBigL, outBigR};
   panel.noteOn(69, 1.0f);
   h.process(256, bigIn, bigOut);
   float loudest = 0.0f;
   for (float v : outBigL)
      loudest = std::max(loudest, std::fabs(v));
   JCLAP_CHECK(loudest > 0.01f);
   JCLAP_CHECK(panel.noteHeld(69));
   JCLAP_CHECK_EQ(panel.noteCount(), 1u);
   // Expression from the note grid reaches the voice the grid shows.
   panel.noteExpression(69, 2.0f, 0.5f, 0.25f);
   h.process(4, in, out);
   EditorHost::Voice voice{};
   JCLAP_CHECK(panel.voice(voice) && voice.note == 69 && voice.bend == 2.0f && voice.timbre == 0.25f);
   panel.noteOff(69);
   h.process(256, bigIn, bigOut);
   JCLAP_CHECK(!panel.noteHeld(69));
   // Host notes count too.
   h.midi(0x90, 60, 100);
   h.process(4, in, out);
   JCLAP_CHECK(panel.noteHeld(60));
   JCLAP_CHECK_EQ(panel.noteCount(), 2u);
   float scope[64];
   panel.readScope(scope, 64);
   JCLAP_CHECK(h.paramText(kTranspose, -12.0) == "-12 st");
   JCLAP_CHECK(h.paramText(kWave, 3.0) == "Square");

   // Every param's text reads back as the same value, as hosts (and clap-validator) check.
   for (clap_id id = 0; id < kParamCount; ++id) {
      for (double v : {kParams[id].min, kParams[id].defaultValue, kParams[id].max}) {
         double back = -999;
         JCLAP_CHECK(h.textToValue(id, h.paramText(id, v).c_str(), back));
         JCLAP_CHECK(h.paramText(id, back) == h.paramText(id, v));
      }
   }

   return jclap::core::testResult();
}
