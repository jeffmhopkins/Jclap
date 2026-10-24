// Drives the plugin the way a host does. Start your own tests from these.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "editor.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace plugin_template;

namespace {

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
   template <typename T> void put(const T &v) {
      const auto *p = reinterpret_cast<const uint8_t *>(&v);
      bytes.insert(bytes.end(), p, p + sizeof(T));
   }
};

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.plugin-template");

   float inL[4] = {0.5f, 0.5f, 0.5f, 0.5f}, inR[4] = {0.5f, 0.5f, 0.5f, 0.5f}, outL[4], outR[4];
   float *in[2] = {inL, inR}, *out[2] = {outL, outR};
   // Gain, pan and width glide over about 20 ms, so the mixer checks run long enough to settle and
   // then look at the last sample, with the input held at inL[0] / inR[0].
   std::vector<float> longL(1024), longR(1024), longOutL(1024), longOutR(1024);
   const auto settle = [&] {
      std::fill(longL.begin(), longL.end(), inL[0]);
      std::fill(longR.begin(), longR.end(), inR[0]);
      float *li[2] = {longL.data(), longR.data()}, *lo[2] = {longOutL.data(), longOutR.data()};
      for (int i = 0; i < 4; ++i)
         h.process(1024, li, lo);
      outL[0] = longOutL.back();
      outR[0] = longOutR.back();
   };

   // Defaults: 0 dB, centre pan, so audio passes unchanged.
   settle();
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f) < 1e-4f && std::fabs(outR[0] - 0.5f) < 1e-4f);

   // -6 dB halves it (near enough); hard left silences the right.
   h.setParam(kGain, -6.0206);
   h.setParam(kPan, -1.0);
   settle();
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f * std::sqrt(2.0f) * 0.5f) < 1e-3f);
   JCLAP_CHECK(std::fabs(outR[0]) < 1e-6f);

   // Swap routing at full width moves the left input to the right output.
   h.setParam(kPan, 0.0);
   h.setParam(kRouting, 1.0);
   inR[0] = 0.0f;
   settle();
   JCLAP_CHECK(std::fabs(outL[0]) < 1e-4f && outR[0] > 0.1f);
   // Width 0 folds to mono.
   h.setParam(kWidth, 0.0);
   settle();
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
   settle();
   JCLAP_CHECK(std::fabs(outL[0] - 0.5f) < 1e-3f);
   JCLAP_CHECK_EQ(h.paramValue(kGain), -6.0206);
   JCLAP_CHECK(std::fabs(h.object<EditorHost>()->paramModulated(kGain)) < 1e-9);
   h.modulate(kGain, 0.0);
   h.setParam(kWidth, 0.0);

   // Mute.
   h.setParam(kMute, 1.0);
   settle();
   JCLAP_CHECK(std::fabs(outL[0]) < 1e-6f);

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

   // A gain change glides instead of jumping in one sample (#51).
   h.setParam(kGain, 0.0);
   h.setParam(kPan, 0.0);
   h.setParam(kWidth, 1.0);
   h.setParam(kMute, 0.0);
   inL[0] = inR[0] = 0.5f;
   settle();
   h.setParam(kGain, -60.0);
   {
      std::fill(longL.begin(), longL.end(), 0.5f);
      std::fill(longR.begin(), longR.end(), 0.5f);
      float *li[2] = {longL.data(), longR.data()}, *lo[2] = {longOutL.data(), longOutR.data()};
      h.process(1024, li, lo);
      JCLAP_CHECK(longOutL[0] > 0.45f);  // still near the old level
      JCLAP_CHECK(longOutL[1023] < 0.01f); // nearly at the new one 21 ms later
      // Mute fades too.
      h.setParam(kGain, 0.0);
      settle();
      h.setParam(kMute, 1.0);
      h.process(1024, li, lo);
      JCLAP_CHECK(longOutL[0] > 0.45f && longOutL[1023] < 0.01f);
      h.setParam(kMute, 0.0);
   }

   // NaN or infinity from the host, a modulator or the panel is ignored (#48).
   h.setParam(kGain, -3.0);
   h.setParam(kGain, std::nan(""));
   h.modulate(kGain, std::numeric_limits<double>::infinity());
   panel.setParam(kGain, std::nan(""));
   settle();
   JCLAP_CHECK_EQ(h.paramValue(kGain), -3.0);
   JCLAP_CHECK(std::isfinite(outL[0]) && outL[0] > 0.1f);
   {
      double v = 0;
      JCLAP_CHECK(!h.textToValue(kGain, "nan", v));
      JCLAP_CHECK(!h.textToValue(kGain, "inf", v));
   }

   // Text entry reads what the display shows (#49): percent for Pan and Width, L/R for pan.
   {
      double v = 0;
      JCLAP_CHECK(h.textToValue(kPan, "30", v) && std::fabs(v - 0.3) < 1e-9);
      JCLAP_CHECK(h.textToValue(kPan, "30%", v) && std::fabs(v - 0.3) < 1e-9);
      JCLAP_CHECK(h.textToValue(kPan, "-50", v) && std::fabs(v + 0.5) < 1e-9);
      JCLAP_CHECK(h.textToValue(kPan, "-50L", v) && std::fabs(v + 0.5) < 1e-9);
      JCLAP_CHECK(h.textToValue(kPan, "50L", v) && std::fabs(v + 0.5) < 1e-9);
      JCLAP_CHECK(h.textToValue(kWidth, "150", v) && std::fabs(v - 1.5) < 1e-9);
      JCLAP_CHECK(h.textToValue(kWidth, "150%", v) && std::fabs(v - 1.5) < 1e-9);
      JCLAP_CHECK(h.paramText(kPan, 0.004) == "C" && h.paramText(kPan, -0.004) == "C");
      JCLAP_CHECK(h.paramText(kPan, 0.006) == "1R");
      JCLAP_CHECK(!h.textToValue(kMute, "Onion", v) && !h.textToValue(kMute, "Offline", v));
      JCLAP_CHECK(h.textToValue(kGain, "100", v) && v == 12.0); // clamped to the range
   }

   // A tone can always be stopped (#50): wildcard note-off, choke, CC 123, CC 120 and reset.
   {
      std::vector<float> zl(1024), zr(1024), ol(1024), orr(1024);
      float *zi[2] = {zl.data(), zr.data()}, *zo[2] = {ol.data(), orr.data()};
      const auto peakAfter = [&](int blocks) {
         float p = 0.0f;
         for (int b = 0; b < blocks; ++b) {
            h.process(1024, zi, zo);
            p = 0.0f;
            for (float x : ol)
               p = std::max(p, std::fabs(x));
         }
         return p;
      };
      h.setParam(kGain, 0.0);
      h.setParam(kRelease, 1.0);
      const auto playing = [&] {
         h.note(CLAP_EVENT_NOTE_ON, 64, 1.0);
         return peakAfter(1) > 0.01f;
      };
      JCLAP_CHECK(playing());
      h.note(CLAP_EVENT_NOTE_OFF, -1, 0.0);
      JCLAP_CHECK(peakAfter(2) < 1e-6f);
      JCLAP_CHECK(!panel.noteHeld(64));

      JCLAP_CHECK(playing());
      h.note(CLAP_EVENT_NOTE_CHOKE, 64, 0.0);
      JCLAP_CHECK(peakAfter(1) < 1e-6f);

      h.setParam(kRelease, 4000.0); // CC 120 has to cut even a long release
      JCLAP_CHECK(playing());
      h.midi(0xB0, 120, 0);
      JCLAP_CHECK(peakAfter(1) < 1e-6f);
      JCLAP_CHECK(!panel.noteHeld(64));

      h.setParam(kRelease, 1.0);
      JCLAP_CHECK(playing());
      h.midi(0xB0, 123, 0);
      JCLAP_CHECK(peakAfter(2) < 1e-6f);

      JCLAP_CHECK(playing());
      h.plugin()->reset(h.plugin());
      JCLAP_CHECK(peakAfter(1) < 1e-6f);
      JCLAP_CHECK(!panel.noteHeld(64));
   }

   // State: a save loads back, and older or shorter states still load (#37, #48).
   {
      const auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      h.setParam(kGain, 4.0);
      h.setParam(kRelease, 1234.0);
      h.process(); // param events apply on the next block
      Buffer saved;
      JCLAP_CHECK(state->save(h.plugin(), &saved.out));
      h.setParam(kGain, 0.0);
      h.process();
      JCLAP_CHECK(state->load(h.plugin(), &saved.in));
      JCLAP_CHECK_EQ(h.paramValue(kGain), 4.0);
      JCLAP_CHECK_EQ(h.paramValue(kRelease), 1234.0);

      // A 0.6.0 save: version 2, no count, 12 values.
      Buffer v2;
      v2.bytes = {'J', 'T', 'M', 'P'};
      v2.put(uint32_t{2});
      for (clap_id id = 0; id < 12; ++id)
         v2.put(id == kGain ? -9.0 : kParams[id].defaultValue);
      JCLAP_CHECK(state->load(h.plugin(), &v2.in));
      JCLAP_CHECK_EQ(h.paramValue(kGain), -9.0);

      // One with fewer params than this build (as when a param is appended later): the rest go back
      // to their defaults. A NaN entry is skipped, not fatal.
      Buffer shorter;
      shorter.bytes = {'J', 'T', 'M', 'P'};
      shorter.put(uint32_t{3});
      shorter.put(uint32_t{3});
      shorter.put(-12.0);
      shorter.put(std::nan(""));
      shorter.put(1.0);
      h.setParam(kPan, 0.5);
      h.process();
      JCLAP_CHECK(state->load(h.plugin(), &shorter.in));
      JCLAP_CHECK_EQ(h.paramValue(kGain), -12.0);
      JCLAP_CHECK_EQ(h.paramValue(kPan), 0.0);
      JCLAP_CHECK_EQ(h.paramValue(kMute), 1.0);
      JCLAP_CHECK_EQ(h.paramValue(kRelease), kParams[kRelease].defaultValue);

      // One from a newer build with more params: the extras are ignored.
      Buffer longer;
      longer.bytes = {'J', 'T', 'M', 'P'};
      longer.put(uint32_t{3});
      longer.put(uint32_t{kParamCount + 2});
      for (clap_id id = 0; id < kParamCount; ++id)
         longer.put(id == kGain ? 2.0 : kParams[id].defaultValue);
      longer.put(7.0);
      longer.put(7.0);
      JCLAP_CHECK(state->load(h.plugin(), &longer.in));
      JCLAP_CHECK_EQ(h.paramValue(kGain), 2.0);

      // A truncated state is refused.
      Buffer cut;
      cut.bytes = {'J', 'T', 'M', 'P'};
      cut.put(uint32_t{3});
      cut.put(uint32_t{kParamCount});
      cut.put(1.0);
      JCLAP_CHECK(!state->load(h.plugin(), &cut.in));
   }

   return jclap::core::testResult();
}
