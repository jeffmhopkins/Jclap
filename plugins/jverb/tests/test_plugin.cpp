// Drives JVerb the way a host does: audio, params, state, presets, and the panel's side of things.

#include <jclap/core/test_host.h>

#include <clap/ext/tail.h>
#include <fstream>

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
#include "orbit.h"
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
   // the echo in the loop past 100%, every modulation at its extreme, then freeze. It stays finite and bounded.
   for (int bodies = 2; bodies <= kMaxBodies; ++bodies) {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kBodies, bodies);
      h.setParam(kDecay, 60.0);
      h.setParam(kFeedback, 1.5);
      h.setParam(kShimmer, 1.0);
      h.setParam(kDriveOn, 1.0);
      h.setParam(kDriveWhere, static_cast<double>(DriveWhere::Loop));
      h.setParam(kDriveType, static_cast<double>(DriveType::Fuzz));
      h.setParam(kDrive, 36.0);
      h.setParam(kEchoOn, 1.0);
      h.setParam(kEchoWhere, static_cast<double>(EchoWhere::Loop));
      h.setParam(kEchoFeedback, 1.05);
      h.setParam(kEchoTime, 120.0);
      h.setParam(kGravity, 4.0);
      h.setParam(kReact, 1.0);
      h.setParam(kMotion, 2.0);
      h.setParam(kOrbitPan, 2.0);
      h.setParam(kOrbitTone, 2.0);
      h.setParam(kOrbitSwirl, 2.0);
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

   // Shimmer and Tilt are heard with Feedback at 0, and the drive straight from its default slot:
   // each changes the tail on its own.
   {
      const auto render = [](clap_id id, double value) {
         jclap::core::TestHost h(clap_entry, kId, kRate);
         h.setParam(kMix, 1.0);
         if (id == kDrive)
            h.setParam(kDriveOn, 1.0);
         h.setParam(id, value);
         return tail(h, kRate, 3.0);
      };
      const auto plain = render(kMix, 1.0);
      for (const auto &[id, value] : {std::pair<clap_id, double>{kShimmer, 1.0}, {kTilt, 1.0}, {kTilt, -1.0}, {kDrive, 30.0}}) {
         const auto changed = render(id, value);
         double diff = 0.0;
         for (size_t i = 0; i < plain.size(); ++i)
            diff += (changed[i] - plain[i]) * (changed[i] - plain[i]);
         // The difference is at least 30 dB under the tail (Shimmer, on noise, is the faintest at about -22 dB).
         JCLAP_CHECK(diff > 0.001 * energy(plain, 0.0, 4.0));
      }
   }

   // Past about 105% the loop holds the tail up instead of letting it die, alone or through the loop
   // drive (whose makeup lets some of its gain through there). The output stays bounded.
   for (const bool drive : {false, true}) {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      h.setParam(kFeedback, 1.5);
      h.setParam(kMix, 1.0);
      if (drive) {
         h.setParam(kDriveOn, 1.0);
         h.setParam(kDriveWhere, static_cast<double>(DriveWhere::Loop));
         h.setParam(kDriveType, static_cast<double>(DriveType::Fuzz));
         h.setParam(kDrive, 30.0);
      }
      const auto out = tail(h, kRate, 10.0);
      JCLAP_CHECK(energy(out, 9.0, 11.0) > 0.5 * energy(out, 1.0, 3.0));
      JCLAP_CHECK(finiteAndBounded(out, 1.25f));
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
      // Frozen, the tank keeps its level and treble (the engine tests check it holds), so what
      // moves here is the bodies' voices swinging their tone and pan: a few dB either way, never
      // a climb.
      for (double w : e)
         JCLAP_CHECK(w < e.front() * 2.5);
      JCLAP_CHECK(e.back() > e.front() * 0.25);
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
      JCLAP_CHECK(h.paramText(bodyParam(2, kBody1Target), static_cast<double>(Target::Damping)) == "Damping");
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

      // A version 1 save: its body targets are translated, and the pitch-bending ones (Delay Mod,
      // Size, Echo) come back Off.
      uint32_t version = 1;
      std::memcpy(buffer.data.data() + 4, &version, sizeof(version));
      const double oldTargets[kMaxBodies] = {1, 2, 3, 5, 7, 8}; // Delay Mod, Size, Drive, Feedback, Width, Echo
      for (int body = 0; body < kMaxBodies; ++body)
         std::memcpy(buffer.data.data() + 12 + 8 * bodyParam(body, kBody1Target), &oldTargets[body], sizeof(double));
      jclap::core::TestHost c(clap_entry, kId, kRate);
      buffer.read = 0;
      const clap_istream_t old = buffer.in();
      JCLAP_CHECK(stateOf(c)->load(c.plugin(), &old));
      const Target expect[kMaxBodies] = {Target::Off, Target::Off, Target::Drive, Target::Feedback, Target::Width, Target::Off};
      for (int body = 0; body < kMaxBodies; ++body)
         JCLAP_CHECK_EQ(c.paramValue(bodyParam(body, kBody1Target)), static_cast<double>(expect[body]));
      JCLAP_CHECK_EQ(c.paramValue(kDecay), 12.5);
   }

   // Nothing moves the sound that the panel doesn't show: bodies start routed nowhere, and a body
   // routed to a param moves that param's knob (its modulated value) while the orbits run.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      for (int body = 0; body < kMaxBodies; ++body)
         JCLAP_CHECK_EQ(h.paramValue(bodyParam(body, kBody1Target)), static_cast<double>(Target::Off));
      EditorHost &panel = *h.object<EditorHost>();
      std::vector<float> inL(static_cast<size_t>(kRate / 4), 0.0f), inR = inL, outL, outR;
      run(h, inL, inR, outL, outR, 256);
      JCLAP_CHECK_EQ(panel.paramModulated(kDampHigh), h.paramValue(kDampHigh));
      h.setParam(bodyParam(0, kBody1Target), static_cast<double>(Target::Damping));
      h.setParam(bodyParam(0, kBody1Amount), 1.0);
      double lo = 1e9, hi = 0.0;
      for (int i = 0; i < 40; ++i) {
         run(h, inL, inR, outL, outR, 256);
         lo = std::min(lo, panel.paramModulated(kDampHigh));
         hi = std::max(hi, panel.paramModulated(kDampHigh));
      }
      JCLAP_CHECK(hi / lo > 1.2);
      JCLAP_CHECK_EQ(h.paramValue(kDampHigh), kParams[kDampHigh].defaultValue);
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
         // Loaded over another preset (one with every body routed), it sets every param: its own
         // values, the rest to defaults.
         const auto riot = std::find_if(factoryPresets().begin(), factoryPresets().end(),
                                        [](const Preset &q) { return q.name == "Orbit Riot"; });
         JCLAP_CHECK(riot != factoryPresets().end());
         applyPreset(p.name == riot->name ? factoryPresets()[1] : *riot, *h.object<EditorHost>());
         applyPreset(p, *h.object<EditorHost>());
         for (clap_id id = 0; id < kParamCount; ++id) {
            double expect = kParams[id].defaultValue;
            for (const auto &[pid, v] : p.values)
               if (pid == id)
                  expect = v;
            JCLAP_CHECK_EQ(h.paramValue(id), expect);
         }
         Noise noise;
         std::vector<float> inL(static_cast<size_t>(kRate)), inR(inL.size()), outL, outR;
         for (size_t i = 0; i < inL.size() / 4; ++i)
            inL[i] = inR[i] = 0.5f * noise.next();
         run(h, inL, inR, outL, outR, 512);
         if (!finiteAndBounded(outL, 4.0f))
            std::printf("preset %s misbehaves\n", p.name.c_str());
         JCLAP_CHECK(finiteAndBounded(outL, 4.0f) && finiteAndBounded(outR, 4.0f));

         // Then 40 s of silence, at a lower rate to keep the test quick: the tail must never grow,
         // even with the drive or the echo in the loop. The Extremes run their loops past 100% on
         // purpose, so theirs may sustain and grow, but only up to the safety stage and the guard.
         constexpr double kLongRate = 24000.0;
         jclap::core::TestHost slow(clap_entry, kId, kLongRate);
         applyPreset(p, *slow.object<EditorHost>());
         const auto longTail = tail(slow, kLongRate, 40.0);
         if (p.category == "Extremes") {
            JCLAP_CHECK(finiteAndBounded(longTail, 1.25f));
            continue;
         }
         const auto e = windows(longTail, kLongRate, 2.0, 4.0);
         // Repeats and routed bodies make a tail rise and fall, but it never climbs above where it
         // has already been.
         bool grows = false;
         double loudest = e.front();
         for (size_t i = 1; i < e.size(); ++i) {
            grows |= e[i] > 1e-6 && e[i] > loudest;
            loudest = std::max(loudest, e[i]);
         }
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
      JCLAP_CHECK(p.name == "jverb-test" && p.values.size() == kParamCount);
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

   // A NaN or infinite input sample is heard as silence: the tail keeps working, the panel's band
   // levels don't stick at full glow, and kicks still register afterwards (#60).
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      EditorHost &panel = *h.object<EditorHost>();
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(kRate)), inR(inL.size()), outL, outR;
      for (size_t i = 0; i < inL.size(); ++i)
         inL[i] = inR[i] = i < 4800 ? 0.5f * noise.next() : 0.0f;
      inL[100] = std::nanf("");
      inR[200] = std::numeric_limits<float>::infinity();
      run(h, inL, inR, outL, outR, 512);
      JCLAP_CHECK(finiteAndBounded(outL, 4.0f) && finiteAndBounded(outR, 4.0f));
      JCLAP_CHECK(energy(outL, 0.2, 0.5) > 1e-6); // the tail still rang, so the tank wasn't reset
      OrbitFrame f;
      JCLAP_CHECK(panel.readFrame(f));
      JCLAP_CHECK(f.band[0] < 0.1f && f.band[1] < 0.1f && f.band[2] < 0.1f); // a second of silence
      const uint32_t hitsBefore = f.hits[0] + f.hits[1] + f.hits[2];
      std::fill(inL.begin(), inL.end(), 0.0f);
      for (size_t i = 0; i < 2400; ++i)
         inL[i] = inR[i] = 0.9f * noise.next();
      run(h, inL, inR, outL, outR, 512);
      JCLAP_CHECK(panel.readFrame(f));
      JCLAP_CHECK(f.hits[0] + f.hits[1] + f.hits[2] > hitsBefore);
   }

   // Automation lands on its own sample, not the start of its block: the same render at any block
   // size with params moving too.
   {
      Noise noise;
      std::vector<float> inL(static_cast<size_t>(kRate)), inR(inL.size());
      for (size_t i = 0; i < inL.size(); ++i)
         inL[i] = inR[i] = (i / 4000) % 3 == 0 ? 0.5f * noise.next() : 0.0f;
      struct Move {
         size_t at;
         clap_id id;
         double value;
      };
      const Move moves[] = {{3000, kDecay, 1.0}, {9001, kSize, 2.0}, {20000, kPredelay, 120.0}, {30500, kMix, 0.8}};
      const auto render = [&](uint32_t block, std::vector<float> &outL) {
         jclap::core::TestHost h(clap_entry, kId, kRate);
         outL.assign(inL.size(), 0.0f);
         std::vector<float> outR(inL.size());
         for (size_t pos = 0; pos < inL.size(); pos += block) {
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
            for (const Move &m : moves)
               if (m.at >= pos && m.at < pos + n)
                  h.setParam(m.id, m.value, static_cast<uint32_t>(m.at - pos));
            float *in[2] = {inL.data() + pos, inR.data() + pos};
            float *out[2] = {outL.data() + pos, outR.data() + pos};
            h.process(n, in, out);
         }
      };
      std::vector<float> a, b;
      render(1024, a);
      render(37, b);
      float worst = 0.0f;
      for (size_t i = 0; i < a.size(); ++i)
         worst = std::max(worst, std::fabs(a[i] - b[i]));
      JCLAP_CHECK(worst < 1e-6f);
   }

   // Silent blocks with buffers, so control ticks run.
   float silence[2][512] = {}, sink[2][512];
   float *silentIn[2] = {silence[0], silence[1]}, *silentOut[2] = {sink[0], sink[1]};

   // The tail extension: finite, longer with a longer decay, infinite when frozen.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      auto *tail = static_cast<const clap_plugin_tail_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_TAIL));
      JCLAP_CHECK(tail != nullptr);
      if (tail) {
         h.process(512, silentIn, silentOut);
         const uint32_t base = tail->get(h.plugin());
         JCLAP_CHECK(base > kRate && base < INT32_MAX);
         h.setParam(kDecay, 20.0);
         h.process(512, silentIn, silentOut);
         JCLAP_CHECK(tail->get(h.plugin()) > 2 * base);
         h.setParam(kFreeze, 1.0);
         h.process(512, silentIn, silentOut);
         JCLAP_CHECK(tail->get(h.plugin()) >= INT32_MAX);
      }
   }

   // Removing a body removes that one: the bodies after it move down, and a drag can't put a body
   // far outside the stage.
   {
      jclap::core::TestHost h(clap_entry, kId, kRate);
      EditorHost &panel = *h.object<EditorHost>();
      h.setParam(kBodies, 4.0);
      h.setParam(kRun, 0.0);
      for (int i = 0; i < 20; ++i)
         h.process(512, silentIn, silentOut);
      OrbitFrame before{}, after{};
      while (panel.readFrame(before)) {
      }
      h.process(512, silentIn, silentOut);
      JCLAP_CHECK(panel.readFrame(before) && before.count == 4);
      panel.send({OrbitCommand::Kind::Remove, 1, 0.0f, 0.0f});
      panel.setParam(kBodies, 3.0);
      for (int i = 0; i < 4; ++i)
         h.process(512, silentIn, silentOut);
      JCLAP_CHECK(panel.readFrame(after) && after.count == 3);
      // Paused, the bodies stay put; the centre of mass is kept at the origin, so they shift by as
      // much as the removed body's share moved it.
      const float shiftX = after.x[1] - before.x[2], shiftY = after.y[1] - before.y[2];
      JCLAP_CHECK(std::fabs(after.x[2] - before.x[3] - shiftX) < 1e-4f && std::fabs(after.y[2] - before.y[3] - shiftY) < 1e-4f);
      JCLAP_CHECK(std::fabs(after.x[0] - before.x[0] - shiftX) < 1e-4f);

      panel.send({OrbitCommand::Kind::Place, 0, 1000.0f, 0.0f});
      for (int i = 0; i < 4; ++i)
         h.process(512, silentIn, silentOut);
      JCLAP_CHECK(panel.readFrame(after) && std::hypot(after.x[0], after.y[0]) < 8.0f);
   }

   // Left running, the bodies keep their energy and stay apart: they used to bleed it away until they
   // sat in pairs on top of each other. Five minutes at the control rate, at the default masses.
   {
      const double mass[kMaxBodies] = {1.0, 1.25, 0.8, 1.0, 1.0, 1.0}, speed[kMaxBodies] = {1, 1, 1, 1, 1, 1};
      for (int count : {3, 6}) {
         OrbitSim sim;
         sim.reset(count, mass, speed, 1.0);
         const double start = sim.energy(mass, 1.0), dt = 0.8 * 32 / kRate;
         const long ticks = static_cast<long>(300 * kRate / 32), lastMinute = static_cast<long>(60 * kRate / 32);
         double nearest = 0.0;
         for (long t = 0; t < ticks; ++t) {
            sim.step(dt, mass, 1.0);
            if (t >= ticks - lastMinute)
               for (int b = 0; b < count; ++b)
                  nearest += sim.nearest(b) / (count * lastMinute);
         }
         JCLAP_CHECK(sim.finite());
         JCLAP_CHECK(std::fabs(sim.energy(mass, 1.0) - start) < 0.02 * std::fabs(start));
         JCLAP_CHECK(nearest > 0.6);
      }
   }

   // A preset file from 0.4.x (69 params) gets its body targets translated; one from 0.5 or 0.6
   // (more params, the same header) doesn't.
   {
      const auto write = [](const std::filesystem::path &file, clap_id count, double target) {
         std::ofstream out(file);
         out << "JVerb preset 1\n";
         for (clap_id id = 0; id < count; ++id)
            out << id << " " << (id == bodyParam(0, kBody1Target) ? target : kParams[id].defaultValue) << "\n";
      };
      const auto folder = std::filesystem::temp_directory_path();
      Preset p;
      write(folder / "jverb-old.jvpreset", kV1ParamCount, 3.0); // 0.4's Drive
      JCLAP_CHECK(readPreset(folder / "jverb-old.jvpreset", p));
      for (const auto &[id, v] : p.values)
         if (id == bodyParam(0, kBody1Target))
            JCLAP_CHECK(v == static_cast<double>(Target::Drive));
      write(folder / "jverb-new.jvpreset", kParamCount, 3.0); // today's Diffusion
      JCLAP_CHECK(readPreset(folder / "jverb-new.jvpreset", p));
      for (const auto &[id, v] : p.values)
         if (id == bodyParam(0, kBody1Target))
            JCLAP_CHECK(v == 3.0);
      std::filesystem::remove(folder / "jverb-old.jvpreset");
      std::filesystem::remove(folder / "jverb-new.jvpreset");
   }

   return jclap::core::testResult();
}
