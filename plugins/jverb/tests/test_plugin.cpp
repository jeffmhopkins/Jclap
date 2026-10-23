// Drives JVerb the way a host does: audio, params, state, presets, and the panel's side of things.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jverb;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jverb";
constexpr double kRate = 48000.0;

// Repeatable noise.
struct Noise {
   uint32_t s = 1;
   float next() {
      s = s * 1664525u + 1013904223u;
      return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
   }
};

// Runs `input` (stereo, interleaved per channel) through `h` in blocks of `block`.
void run(jclap::core::TestHost &h, const std::vector<float> &inL, const std::vector<float> &inR,
         std::vector<float> &outL, std::vector<float> &outR, uint32_t block) {
   outL.assign(inL.size(), 0.0f);
   outR.assign(inR.size(), 0.0f);
   for (size_t pos = 0; pos < inL.size(); pos += block) {
      const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
      float *in[2] = {const_cast<float *>(inL.data() + pos), const_cast<float *>(inR.data() + pos)};
      float *out[2] = {outL.data() + pos, outR.data() + pos};
      h.process(n, in, out);
   }
}

bool finiteAndBounded(const std::vector<float> &v, float limit) {
   return std::all_of(v.begin(), v.end(), [&](float x) { return std::isfinite(x) && std::fabs(x) < limit; });
}

double energy(const std::vector<float> &v, double fromSeconds, double toSeconds) {
   double e = 0.0;
   for (size_t i = static_cast<size_t>(fromSeconds * kRate); i < static_cast<size_t>(toSeconds * kRate) && i < v.size(); ++i)
      e += v[i] * v[i];
   return e;
}

// Energy in each `window`-second stretch of `v` at `rate`, from `from` seconds on.
std::vector<double> windows(const std::vector<float> &v, double rate, double from, double window) {
   std::vector<double> out;
   const size_t n = static_cast<size_t>(window * rate);
   for (size_t start = static_cast<size_t>(from * rate); start + n <= v.size(); start += n) {
      double e = 0.0;
      for (size_t i = start; i < start + n; ++i)
         e += v[i] * v[i];
      out.push_back(e);
   }
   return out;
}

// One second of noise into `h` (running at `rate`), then `seconds` of silence; the output's left
// channel.
std::vector<float> tail(jclap::core::TestHost &h, double rate, double seconds) {
   Noise noise;
   std::vector<float> inL(static_cast<size_t>((1.0 + seconds) * rate), 0.0f), inR(inL.size()), outL, outR;
   for (size_t i = 0; i < static_cast<size_t>(rate); ++i) {
      inL[i] = 0.5f * noise.next();
      inR[i] = 0.5f * noise.next();
   }
   run(h, inL, inR, outL, outR, 512);
   return outL;
}

// A memory stream for the state extension.
struct Buffer {
   std::vector<char> data;
   size_t read = 0;
   clap_ostream_t out() {
      return {this, [](const clap_ostream_t *s, const void *p, uint64_t n) -> int64_t {
                 auto *b = static_cast<Buffer *>(s->ctx);
                 b->data.insert(b->data.end(), static_cast<const char *>(p), static_cast<const char *>(p) + n);
                 return static_cast<int64_t>(n);
              }};
   }
   clap_istream_t in() {
      return {this, [](const clap_istream_t *s, void *p, uint64_t n) -> int64_t {
                 auto *b = static_cast<Buffer *>(s->ctx);
                 const size_t k = std::min<size_t>(n, b->data.size() - b->read);
                 std::memcpy(p, b->data.data() + b->read, k);
                 b->read += k;
                 return static_cast<int64_t>(k);
              }};
   }
};

const clap_plugin_state_t *stateOf(jclap::core::TestHost &h) {
   return static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
}

} // namespace

int main() {
   // Silence in, silence out, and an impulse leaves a tail that dies away.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kDecay, 1.0);
      std::vector<float> inL(static_cast<size_t>(4 * kRate), 0.0f), inR = inL, outL, outR;
      run(h, inL, inR, outL, outR, 512);
      JCLAP_CHECK(energy(outL, 0, 4) == 0.0);
      inL[0] = inR[0] = 1.0f;
      run(h, inL, inR, outL, outR, 512);
      JCLAP_CHECK(energy(outL, 0.05, 0.5) > 1e-4);
      JCLAP_CHECK(energy(outL, 3.5, 4.0) < energy(outL, 0.05, 0.5) * 1e-4);
   }

   // The same input gives the same output whatever the block size: the orbits, the analyzer and
   // every control tick run on sample counts, never on blocks.
   {
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(2 * kRate)), inR(inL.size());
      for (size_t i = 0; i < inL.size(); ++i) {
         const float burst = (i / 6000) % 4 == 0 ? 0.8f : 0.0f;
         inL[i] = burst * noise.next();
         inR[i] = burst * noise.next();
      }
      std::vector<float> aL, aR, bL, bR;
      jclap::core::TestHost a(clap_entry, kId, kRate), b(clap_entry, kId, kRate);
      for (auto *h : {&a, &b}) {
         h->setParam(kReact, 1.0);
         h->setParam(kEchoOn, 1.0);
      }
      run(a, inL, inR, aL, aR, 1024);
      run(b, inL, inR, bL, bR, 37);
      float worst = 0.0f;
      for (size_t i = 0; i < aL.size(); ++i)
         worst = std::max({worst, std::fabs(aL[i] - bL[i]), std::fabs(aR[i] - bR[i])});
      JCLAP_CHECK(worst < 1e-6f);
   }

   // Pushed as hard as it goes, at every body count: feedback past 100%, the loop drive, shimmer,
   // the echo in the loop past 100%, then freeze. It stays finite and bounded.
   for (int bodies = 2; bodies <= kMaxBodies; ++bodies) {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kBodies, bodies);
      h.setParam(kDecay, 60.0);
      h.setParam(kFeedback, 1.1);
      h.setParam(kShimmer, 1.0);
      h.setParam(kDriveOn, 1.0);
      h.setParam(kDriveType, static_cast<double>(DriveType::Fuzz));
      h.setParam(kDrive, 36.0);
      h.setParam(kEchoOn, 1.0);
      h.setParam(kEchoWhere, static_cast<double>(EchoWhere::Loop));
      h.setParam(kEchoFeedback, 1.05);
      h.setParam(kEchoTime, 120.0);
      h.setParam(kGravity, 4.0);
      h.setParam(kReact, 1.0);
      h.setParam(kMix, 1.0);
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(3 * kRate)), inR(inL.size()), outL, outR;
      for (size_t i = 0; i < inL.size() / 3; ++i)
         inL[i] = inR[i] = noise.next();
      run(h, inL, inR, outL, outR, 256);
      JCLAP_CHECK(finiteAndBounded(outL, 4.0f) && finiteAndBounded(outR, 4.0f));
      h.setParam(kFreeze, 1.0);
      run(h, inL, inR, outL, outR, 256);
      JCLAP_CHECK(finiteAndBounded(outL, 4.0f) && finiteAndBounded(outR, 4.0f));
   }

   // Long tails die away and never grow, however long the decay: the tank's gain stays below 1 at
   // every frequency. These run for a minute because a gain only just above 1 takes that long to
   // show.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kDecay, 60.0);
      h.setParam(kMix, 1.0);
      const auto e = windows(tail(h, kRate, 60.0), kRate, 2.0, 5.0);
      // 60 s to fall 60 dB: about 6 dB of energy lost every 5 s window, 50 dB over the ten.
      for (size_t i = 1; i < e.size(); ++i)
         JCLAP_CHECK(e[i] < e[i - 1]);
      JCLAP_CHECK(e.back() < e.front() * 1e-3);
      JCLAP_CHECK(e.back() > e.front() * 1e-8);
   }
   // The decay time is about what the knob says: a 2 s tail falls 30 dB in 1 s.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kDecay, 2.0);
      h.setParam(kMix, 1.0);
      h.setParam(kMotion, 0.0);
      const auto e = windows(tail(h, kRate, 4.0), kRate, 1.5, 0.5);
      const double dbPerSecond = 10.0 * std::log10(e[2] / e[0]);
      JCLAP_CHECK(dbPerSecond < -22.0 && dbPerSecond > -40.0);
   }
   // Freeze holds the tail without letting it grow, at every body count.
   for (int bodies = 2; bodies <= kMaxBodies; bodies += 2) {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kBodies, bodies);
      h.setParam(kMix, 1.0);
      h.setParam(kDecay, 10.0);
      std::vector<float> inL(static_cast<size_t>(kRate / 2)), inR(inL.size()), outL, outR;
      Noise noise;
      for (auto &x : inL)
         x = 0.5f * noise.next();
      inR = inL;
      run(h, inL, inR, outL, outR, 512);
      h.setParam(kFreeze, 1.0);
      const auto e = windows(tail(h, kRate, 40.0), kRate, 3.0, 5.0);
      for (double w : e)
         JCLAP_CHECK(w < e.front() * 1.5);
      JCLAP_CHECK(e.back() > e.front() * 0.1);
   }

   // A non-finite value or modulation from the host is ignored, and the orbits keep going.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kGravity, std::nan(""));
      h.modulate(kGravity, std::nan(""));
      h.modulate(kSize, std::numeric_limits<double>::infinity());
      h.object<EditorHost>()->setParam(kDecay, std::nan(""));
      JCLAP_CHECK(std::isfinite(h.paramValue(kGravity)) && std::isfinite(h.paramValue(kDecay)));
      const auto out = tail(h, kRate, 1.0);
      JCLAP_CHECK(finiteAndBounded(out, 4.0f));
      JCLAP_CHECK(energy(out, 1.0, 2.0) > 1e-3);
      OrbitFrame f;
      JCLAP_CHECK(h.object<EditorHost>()->readFrame(f));
      JCLAP_CHECK(std::isfinite(f.x[0]) && std::isfinite(f.y[0]));
   }

   // The echo, after the reverb, repeats a short room 300 ms later.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kDecay, 0.2);
      h.setParam(kSize, 0.25);
      h.setParam(kPredelay, 0.0);
      h.setParam(kMix, 1.0);
      h.setParam(kEchoOn, 1.0);
      h.setParam(kEchoWhere, static_cast<double>(EchoWhere::After));
      h.setParam(kEchoSync, 0.0);
      h.setParam(kEchoTime, 300.0);
      h.setParam(kEchoFeedback, 0.0);
      h.setParam(kEchoPingPong, 0.0);
      h.setParam(kEchoMix, 1.0);
      std::vector<float> inL(static_cast<size_t>(kRate), 0.0f), inR = inL, outL, outR;
      run(h, inL, inR, outL, outR, 256); // let the time glide settle
      inL[0] = inR[0] = 1.0f;
      run(h, inL, inR, outL, outR, 256);
      JCLAP_CHECK(energy(outL, 0.30, 0.34) > 50.0 * energy(outL, 0.22, 0.26));
   }

   // Every param's text reads back as the same value.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      for (clap_id id = 0; id < kParamCount; ++id)
         for (double v : {kParams[id].min, kParams[id].defaultValue, kParams[id].max}) {
            const std::string text = h.paramText(id, v);
            double back = -1e9;
            const bool ok = h.textToValue(id, text.c_str(), back);
            if (!ok || h.paramText(id, back) != text)
               std::printf("param %u: \"%s\" doesn't round-trip\n", id, text.c_str());
            JCLAP_CHECK(ok && h.paramText(id, back) == text);
         }
      JCLAP_CHECK(h.paramText(kEchoSync, 6.0) == "1/8D");
      JCLAP_CHECK(h.paramText(bodyParam(2, kBody1Target), static_cast<double>(Target::Echo)) == "Echo");
   }

   // State: values and the preset name come back.
   {
      jclap::core::TestHost a(clap_entry, kId, kRate), b(clap_entry, kId, kRate);
      a.setParam(kDecay, 12.5);
      a.setParam(kEchoTime, 777.0);
      a.setParam(bodyParam(4, kBody1Mass), 2.25);
      a.process(64);
      a.object<EditorHost>()->setPresetName("Moon Room");
      Buffer buffer;
      const clap_ostream_t out = buffer.out();
      JCLAP_CHECK(stateOf(a)->save(a.plugin(), &out));
      const clap_istream_t in = buffer.in();
      JCLAP_CHECK(stateOf(b)->load(b.plugin(), &in));
      JCLAP_CHECK_EQ(b.paramValue(kDecay), 12.5);
      JCLAP_CHECK_EQ(b.paramValue(kEchoTime), 777.0);
      JCLAP_CHECK_EQ(b.paramValue(bodyParam(4, kBody1Mass)), 2.25);
      JCLAP_CHECK(b.object<EditorHost>()->presetName() == "Moon Room");
   }

   // Panel edits reach the host as gestures, and the orbit view's commands are taken.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      EditorHost &panel = *h.object<EditorHost>();
      panel.beginEdit(kSize);
      panel.setParam(kSize, 2.0);
      panel.endEdit(kSize);
      const auto edits = h.process(64);
      JCLAP_CHECK_EQ(edits.gestures.size(), 2u);
      JCLAP_CHECK_EQ(h.paramValue(kSize), 2.0);
      std::vector<float> inL(4096, 0.0f), inR = inL, outL, outR;
      run(h, inL, inR, outL, outR, 64);
      OrbitFrame before, after;
      JCLAP_CHECK(panel.readFrame(before));
      // Dropped far out, the body lands well away from where it was (the sim then re-centres the
      // system on its centre of mass, so not exactly where it was dropped).
      panel.send({OrbitCommand::Kind::Place, 0, 2.5f, 2.5f});
      run(h, inL, inR, outL, outR, 64);
      JCLAP_CHECK(panel.readFrame(after));
      JCLAP_CHECK_EQ(after.count, 3);
      JCLAP_CHECK(std::isfinite(after.x[0]) && std::isfinite(after.y[0]));
      JCLAP_CHECK(std::hypot(after.x[0] - before.x[0], after.y[0] - before.y[0]) > 0.8);
   }

   // Clicking quickly through presets: every load reaches the host, and the last one wins, with no
   // gestures left open.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      for (int round = 0; round < 3; ++round)
         for (const Preset &p : factoryPresets())
            applyPreset(p, *h.object<EditorHost>());
      const auto out = h.process(64);
      JCLAP_CHECK(out.gestures.size() == 2 * out.paramValues.size());
      const Preset &last = factoryPresets().back();
      for (const auto &[id, v] : last.values)
         JCLAP_CHECK_EQ(h.paramValue(id), v);
      for (const auto &ev : out.paramValues)
         JCLAP_CHECK_EQ(ev.value, h.paramValue(ev.param_id));
   }

   // After a reset and an orbit restart, the same input gives the same output as a fresh instance,
   // with React on: the kicks come in the same directions.
   {
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(kRate)), inR(inL.size()), aL, aR, bL, bR;
      for (size_t i = 0; i < inL.size(); ++i)
         inL[i] = inR[i] = ((i / 4800) % 2 ? 0.0f : 0.8f) * noise.next();
      jclap::core::TestHost a(clap_entry, kId, kRate), b(clap_entry, kId, kRate);
      for (auto *h : {&a, &b})
         h->setParam(kReact, 1.0);
      run(a, inL, inR, aL, aR, 512);
      a.plugin()->reset(a.plugin());
      a.object<EditorHost>()->send({OrbitCommand::Kind::Reset, 0, 0.0f, 0.0f});
      run(a, inL, inR, aL, aR, 512);
      run(b, inL, inR, bL, bR, 512);
      float worst = 0.0f;
      for (size_t i = 0; i < aL.size(); ++i)
         worst = std::max(worst, std::fabs(aL[i] - bL[i]));
      JCLAP_CHECK(worst < 1e-6f);
   }

   // Factory presets: unique names, every value in range, and each one loads and plays cleanly.
   {
      std::set<std::string> names;
      for (const Preset &p : factoryPresets()) {
         JCLAP_CHECK(names.insert(p.name).second);
         for (const auto &[id, v] : p.values)
            JCLAP_CHECK(id < kParamCount && v >= kParams[id].min && v <= kParams[id].max);
         jclap::core::TestHost h(clap_entry, kId, kRate);
         applyPreset(p, *h.object<EditorHost>());
         Noise noise;
         std::vector<float> inL(static_cast<size_t>(kRate)), inR(inL.size()), outL, outR;
         for (size_t i = 0; i < inL.size() / 4; ++i)
            inL[i] = inR[i] = 0.5f * noise.next();
         run(h, inL, inR, outL, outR, 512);
         if (!finiteAndBounded(outL, 4.0f))
            std::printf("preset %s misbehaves\n", p.name.c_str());
         JCLAP_CHECK(finiteAndBounded(outL, 4.0f) && finiteAndBounded(outR, 4.0f));

         // Then 40 s of silence, at a lower rate to keep the test quick: the tail must never grow,
         // even with the drive or the echo in the loop.
         constexpr double kLongRate = 24000.0;
         jclap::core::TestHost slow(clap_entry, kId, kLongRate);
         applyPreset(p, *slow.object<EditorHost>());
         const auto e = windows(tail(slow, kLongRate, 40.0), kLongRate, 2.0, 4.0);
         bool grows = false;
         for (size_t i = 2; i < e.size(); ++i)
            grows |= e[i] > 1e-6 && e[i] > e[i - 1] * 1.25;
         if (grows || e.back() > e.front())
            std::printf("preset %s: tail grows (%.3g -> %.3g)\n", p.name.c_str(), e.front(), e.back());
         JCLAP_CHECK(!grows && e.back() <= e.front());
      }
      JCLAP_CHECK(names.size() >= 20);
   }

   // User presets write and read back.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      EditorHost &panel = *h.object<EditorHost>();
      panel.setParam(kDecay, 9.0);
      panel.setParam(kEchoOn, 1.0);
      const auto file = std::filesystem::temp_directory_path() / "jverb-test.jvpreset";
      JCLAP_CHECK(writePreset(file, panel));
      Preset p;
      JCLAP_CHECK(readPreset(file, p));
      JCLAP_CHECK(p.name == "jverb-test" && p.values.size() == 2);
      std::filesystem::remove(file);
      JCLAP_CHECK(presetFileName("My/Preset: *1* ") == "MyPreset 1");
   }

   // Rough cost with everything on: six bodies, drive, shimmer, echo. Printed, not checked, since
   // test machines vary.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kBodies, 6.0);
      h.setParam(kDriveOn, 1.0);
      h.setParam(kShimmer, 0.5);
      h.setParam(kFeedback, 0.5);
      h.setParam(kEchoOn, 1.0);
      h.setParam(kAttack, 0.5);
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(10 * kRate)), inR(inL.size()), outL, outR;
      for (size_t i = 0; i < inL.size(); ++i)
         inL[i] = inR[i] = 0.3f * noise.next();
      const auto start = std::chrono::steady_clock::now();
      run(h, inL, inR, outL, outR, 256);
      const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::printf("10 s of audio, everything on: %.3f s (%.1f%% of one core)\n", seconds, seconds * 10.0);
   }

   return jclap::core::testResult();
}
