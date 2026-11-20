// Drives JKit the way a host does: sounds on pads, notes in, audio out, state saved and loaded.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>
#include <vector>

#include "editor.h"
#include "kit.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jkit;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jkit";

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

// A 0.2 s sine burst at 220 Hz with a quick decay.
SamplePtr tone(double seconds = 0.2, int role = kKick) {
   auto s = std::make_shared<Sample>();
   s->name = "Test";
   s->prompt = "a test tone";
   s->role = role;
   s->seed = 7;
   const size_t n = static_cast<size_t>(seconds * kSampleRate);
   for (size_t i = 0; i < n; ++i) {
      const double t = i / kSampleRate;
      const float v = static_cast<float>(0.8 * std::sin(2 * 3.14159265 * 220 * t) * std::exp(-t * 5));
      s->left.push_back(v);
      s->right.push_back(v);
   }
   return s;
}

// Runs `blocks` blocks of 256 and returns the left channel.
std::vector<float> run(jclap::core::TestHost &h, int blocks) {
   std::vector<float> all;
   std::vector<float> l(256), r(256);
   float *out[2] = {l.data(), r.data()};
   for (int b = 0; b < blocks; ++b) {
      h.process(256, nullptr, out);
      all.insert(all.end(), l.begin(), l.end());
   }
   return all;
}

float peak(const std::vector<float> &v, size_t from = 0, size_t to = SIZE_MAX) {
   float p = 0;
   for (size_t i = from; i < std::min(to, v.size()); ++i)
      p = std::max(p, std::fabs(v[i]));
   return p;
}

// The last sample louder than -60 dB.
size_t lastSound(const std::vector<float> &v) {
   size_t last = 0;
   for (size_t i = 0; i < v.size(); ++i)
      if (std::fabs(v[i]) > 0.001f)
         last = i;
   return last;
}

void testParamText() {
   jclap::core::TestHost h(clap_entry, kId);
   for (clap_id id = 0; id < kParamCount; ++id) {
      const auto &d = param(id);
      for (double f : {0.0, 0.13, 0.5, 0.77, 1.0}) {
         double v = d.min + (d.max - d.min) * f;
         if (d.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         const std::string a = h.paramText(id, v);
         double back = 0;
         JCLAP_CHECK(h.textToValue(id, a.c_str(), back));
         const std::string b = h.paramText(id, back);
         if (a != b)
            std::printf("param %u: %s -> %g -> %s\n", id, a.c_str(), back, b.c_str());
         JCLAP_CHECK(a == b);
      }
   }
   JCLAP_CHECK(h.paramText(padParam(0, kDecay), 1.0) == "Full");
   JCLAP_CHECK(h.paramText(padParam(0, kLowCut), 0.0) == "Off");
   JCLAP_CHECK(h.paramText(padParam(0, pointParam(2, kPointType)), kToneBell) == "Bell");

   // A 0.3 project's EQ moves onto points: the low cut onto the high pass, then a low shelf, a bell
   // and a high shelf in the free points, and the old params back to their defaults.
   std::vector<double> v(kParamCount);
   for (clap_id id = 0; id < kParamCount; ++id)
      v[id] = param(id).defaultValue;
   v[padParam(5, kLowCut)] = lowCutValue(80.0);
   v[padParam(5, kMid)] = 4.0;
   v[padParam(5, kMidFreq)] = midFreqValue(2000.0);
   v[padParam(5, kHigh)] = -3.0;
   migrateLegacyEq(5, [&](clap_id id) { return v[id]; }, [&](clap_id id, double x) { v[id] = x; });
   const auto pt = [&](int i, PointField f) { return v[padParam(5, pointParam(i, f))]; };
   JCLAP_CHECK(pt(0, kPointType) == kToneHp12 && std::fabs(toneHz(pt(0, kPointFreq)) - 80.0) < 0.5);
   JCLAP_CHECK(pt(2, kPointType) == kToneBell && pt(2, kPointGain) == 4.0);
   JCLAP_CHECK(std::fabs(toneHz(pt(2, kPointFreq)) - 2000.0) < 5.0);
   JCLAP_CHECK(pt(3, kPointType) == kToneHighShelf && pt(3, kPointGain) == -3.0);
   JCLAP_CHECK(pt(4, kPointType) == kToneOff);
   JCLAP_CHECK(v[padParam(5, kLowCut)] == 0.0 && v[padParam(5, kMid)] == 0.0 && v[padParam(5, kHigh)] == 0.0);
   JCLAP_CHECK(h.paramText(kVolume, -60.0) == "-inf dB");
}

void testPlaying() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   JCLAP_CHECK(host != nullptr);
   // Empty: silence.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 10)) < 1e-6f);
   host->setPadSample(0, tone());
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> a = run(h, 60);
   JCLAP_CHECK(peak(a) > 0.3f);
   // 0.2 s at 48 kHz is 9600 samples (and the master's 30 Hz low cut rings on a little).
   const size_t full = lastSound(a);
   JCLAP_CHECK(full > 9000 && full < 11500);
   // Note 37 is pad 2, which is empty.
   h.note(CLAP_EVENT_NOTE_ON, 37, 1.0);
   JCLAP_CHECK(peak(run(h, 10)) < 1e-6f);
   // MIDI note-ons play too.
   h.midi(0x90, 36, 100);
   JCLAP_CHECK(peak(run(h, 10)) > 0.3f);
   run(h, 60);

   // An octave up plays it in half the time.
   h.setParam(padParam(0, kPitch), 12.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> up = run(h, 60);
   JCLAP_CHECK(lastSound(up) > full * 0.4 && lastSound(up) < full * 0.6);
   h.setParam(padParam(0, kPitch), 0.0);

   // End at half.
   h.setParam(padParam(0, kEnd), 0.5);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> half = run(h, 60);
   JCLAP_CHECK(lastSound(half) > full * 0.4 && lastSound(half) < full * 0.6);
   h.setParam(padParam(0, kEnd), 1.0);

   // A short decay cuts it short.
   h.setParam(padParam(0, kDecay), decayValue(20.0));
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(lastSound(run(h, 60)) < 2000);
   h.setParam(padParam(0, kDecay), 1.0);

   // Velocity: soft is quieter.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float loud = peak(run(h, 60));
   h.note(CLAP_EVENT_NOTE_ON, 36, 0.2);
   const float soft = peak(run(h, 60));
   JCLAP_CHECK(soft < loud * 0.6f && soft > 0.0f);

   // Level at -inf is silent.
   h.setParam(padParam(0, kLevel), -60.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 20)) < 1e-6f);
   h.setParam(padParam(0, kLevel), 0.0);

   // Gate: the note's end stops it; one shot plays on.
   h.setParam(padParam(0, kPlayMode), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 4);
   h.note(CLAP_EVENT_NOTE_OFF, 36, 0.0);
   const std::vector<float> gated = run(h, 40);
   JCLAP_CHECK(lastSound(gated) < 3000);
   h.setParam(padParam(0, kPlayMode), 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 4);
   h.note(CLAP_EVENT_NOTE_OFF, 36, 0.0);
   JCLAP_CHECK(lastSound(run(h, 40)) > 6000);

   // Reverse starts quiet (the tail) and ends loud.
   h.setParam(padParam(0, kReverse), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> rev = run(h, 40);
   JCLAP_CHECK(peak(rev, 0, 1000) < peak(rev, 8000, 9600));
   h.setParam(padParam(0, kReverse), 0.0);
   run(h, 20);

   // PAD TONE: point 2 (a low pass at 20 kHz by default) as a 24 dB low pass at 50 Hz all but
   // silences the 220 Hz tone.
   h.setParam(padParam(0, kVelocity), 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float open = peak(run(h, 60));
   const auto set = [&](int i, PointField f, double v) { h.setParam(padParam(0, pointParam(i, f)), v); };
   set(1, kPointType, kToneLp24);
   set(1, kPointFreq, toneHzValue(50.0));
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float closed = peak(run(h, 60));
   JCLAP_CHECK(closed < open * 0.3f);
   // The envelope opens it at the hit.
   h.setParam(padParam(0, kFilterEnv), 1.0);
   h.setParam(padParam(0, kFilterDecay), filterDecayValue(1000.0));
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 60)) > closed * 2.0f);
   h.setParam(padParam(0, kFilterEnv), 0.0);
   // Velocity to Filter: at 1 kHz, a soft hit closes it below the tone (with the level ignoring velocity).
   set(1, kPointFreq, toneHzValue(1000.0));
   h.setParam(padParam(0, kVelFilter), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float hard = peak(run(h, 60));
   h.note(CLAP_EVENT_NOTE_ON, 36, 0.1);
   const float gentle = peak(run(h, 60));
   JCLAP_CHECK(gentle < hard * 0.5f && hard > open * 0.8f);
   h.setParam(padParam(0, kVelFilter), 0.0);
   set(1, kPointType, kToneLp12);
   set(1, kPointFreq, toneHzValue(20000.0));
   // An added bell at the tone's pitch: +12 dB makes it about 4 times as loud.
   set(3, kPointType, kToneBell);
   set(3, kPointFreq, toneHzValue(220.0));
   set(3, kPointGain, 12.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float boosted = peak(run(h, 60));
   JCLAP_CHECK(boosted > open * 3.0f && boosted < open * 5.0f);
   set(3, kPointType, kToneOff);
   // The default points leave it as it was.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(std::fabs(peak(run(h, 60)) - open) < open * 0.05f);
   // Drive keeps it near its level, but squarer: the peak stays put while the average rises.
   h.setParam(padParam(0, kDrive), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> driven = run(h, 60);
   JCLAP_CHECK(peak(driven) > open * 0.4f && peak(driven) < open * 1.5f);
   h.setParam(padParam(0, kDrive), 0.0);
   h.setParam(padParam(0, kVelocity), 0.6);
   run(h, 20);
}

// A pad with several takes plays one a hit, each with its own trims, level, tune and cuts.
void testTakes() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   const auto hit = [&](float velocity = 1.0f) {
      h.note(CLAP_EVENT_NOTE_ON, 36, velocity);
      return run(h, 80);
   };
   // CYCLE between a 0.1 s and a 0.3 s take: the hits alternate.
   PadSounds p;
   p.mode = kTakeCycle;
   p.takes.push_back(Take{tone(0.1)});
   p.takes.push_back(Take{tone(0.3)});
   host->setPadSounds(0, p);
   JCLAP_CHECK_EQ(host->padSounds(0).count(), 2);
   const size_t a = lastSound(hit()), b = lastSound(hit()), c = lastSound(hit());
   JCLAP_CHECK(a > 4000 && a < 6500 && b > 13500 && b < 16500 && c > 4000 && c < 6500);
   JCLAP_CHECK_EQ(host->padTake(0), 0);
   // The take editor plays the one it asks for.
   host->playPadTake(0, 1);
   JCLAP_CHECK(lastSound(run(h, 80)) > 13500);

   // One take, then its own end, tune, level, low cut and fade in, each against the plain one.
   host->setPadSounds(0, PadSounds::of(tone(0.3)));
   const std::vector<float> plain = hit();
   const size_t full = lastSound(plain);
   const auto with = [&](auto change) {
      Take k{tone(0.3)};
      change(k);
      PadSounds one;
      one.takes.push_back(k);
      host->setPadSounds(0, one);
      return hit();
   };
   JCLAP_CHECK(lastSound(with([](Take &k) { k.end = 0.5; })) < full * 0.6);
   JCLAP_CHECK(lastSound(with([](Take &k) { k.tune = 12.0; })) < full * 0.6);
   JCLAP_CHECK(peak(with([](Take &k) { k.levelDb = -12.0; })) < peak(plain) * 0.3f);
   JCLAP_CHECK(peak(with([](Take &k) { k.lowCutHz = 3000.0; })) < peak(plain) * 0.2f);
   JCLAP_CHECK(peak(with([](Take &k) { k.highCutHz = 20000.0; k.lowCutHz = 20.0; })) > peak(plain) * 0.95f);
   JCLAP_CHECK(peak(with([](Take &k) { k.fadeInMs = 100.0; }), 0, 480) < peak(plain, 0, 480) * 0.3f);
   // The pad's own START is inside the take's: a take starting halfway with the pad's START at
   // half plays the last quarter.
   h.setParam(padParam(0, kStart), 0.5);
   JCLAP_CHECK(lastSound(with([](Take &k) { k.start = 0.5; })) < full * 0.35);
   h.setParam(padParam(0, kStart), 0.0);

   // More than kMaxTakes are dropped; addPadTake stops there.
   host->setPadSample(0, nullptr);
   for (int i = 0; i < kMaxTakes + 2; ++i)
      host->addPadTake(0, tone(0.1));
   JCLAP_CHECK_EQ(host->padSounds(0).count(), kMaxTakes);
   // Swapping pads takes their takes along.
   host->swapPads(0, 7);
   JCLAP_CHECK(host->padSounds(0).empty() && host->padSounds(7).count() == kMaxTakes);
}

void testChokeAndSends() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->setPadSample(0, tone(0.5));
   host->setPadSample(1, tone(0.5));
   h.setParam(padParam(0, kChoke), 1.0);
   h.setParam(padParam(1, kChoke), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 2);
   JCLAP_CHECK(host->padPosition(0) >= 0.0f);
   h.note(CLAP_EVENT_NOTE_ON, 37, 1.0);
   run(h, 4);
   // Pad 1 was cut off by pad 2 in the same group.
   JCLAP_CHECK(host->padPosition(0) < 0.0f);
   JCLAP_CHECK(host->padPosition(1) >= 0.0f);
   run(h, 120);

   // A reverb send rings on after the sound ends.
   h.setParam(padParam(0, kReverbSend), 1.0);
   h.setParam(kReverbReturn, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> wet = run(h, 200);
   JCLAP_CHECK(lastSound(wet) > 30000);
   h.setParam(padParam(0, kReverbSend), 0.0);
   run(h, 600);
   // Delay too.
   h.setParam(padParam(0, kDelaySend), 1.0);
   h.setParam(kDelayReturn, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(lastSound(run(h, 200)) > 30000);
   h.setParam(padParam(0, kDelaySend), 0.0);
   run(h, 1200);

   // The kit sends: the whole kit into the reverb and the delay, with the pad's own sends at 0.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const size_t dry = lastSound(run(h, 200));
   run(h, 600);
   for (clap_id kitSend : {kKitReverbSend, kKitDelaySend}) {
      h.setParam(kitSend, 1.0);
      h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
      JCLAP_CHECK(lastSound(run(h, 200)) > std::max<size_t>(dry, 30000));
      h.setParam(kitSend, 0.0);
      run(h, 1200);
   }
}

void testMaster() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->setPadSample(0, tone());
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float base = peak(run(h, 60));
   h.setParam(kVolume, -12.0);
   run(h, 2);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float quiet = peak(run(h, 60));
   JCLAP_CHECK(std::fabs(quiet / base - 0.251f) < 0.03f);
   h.setParam(kVolume, 0.0);
   // Boom adds low end: a 220 Hz tone is near its edge, a 60 Hz one isn't.
   h.setParam(kBoom, 1.0);
   run(h, 2);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 60)) > base * 1.05f);
}

void testState() {
   Buffer state;
   {
      jclap::core::TestHost h(clap_entry, kId);
      auto *host = h.object<EditorHost>();
      host->setPadSample(5, tone(0.3, kHat));
      Take second{tone(0.2, kHat)};
      second.tune = 3.0;
      second.lowCutHz = 500.0;
      host->addPadTake(5, second.sample);
      PadSounds five = host->padSounds(5);
      five.takes[1] = second;
      five.mode = kTakeVelocity;
      host->setPadSounds(5, five);
      host->setParam(padParam(5, kPitch), -5.0);
      host->setParam(kBoom, 0.4);
      host->gen().prompt = "a saved prompt";
      host->gen().role = kClap;
      host->gen().kitMode = true;
      host->gen().kitLayout = 2;
      host->gen().kitTakes = 4;
      host->setKitName("My Kit");
      auto *st = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      JCLAP_CHECK(st->save(h.plugin(), &state.out));
   }
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   auto *st = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
   JCLAP_CHECK(st->load(h.plugin(), &state.in));
   const SamplePtr s = host->padSample(5);
   JCLAP_CHECK(s && s->frames() == static_cast<size_t>(0.3 * kSampleRate) && s->role == kHat && s->seed == 7);
   const PadSounds five = host->padSounds(5);
   JCLAP_CHECK(five.count() == 2 && five.mode == kTakeVelocity && five.takes[1].tune == 3.0 &&
               five.takes[1].lowCutHz == 500.0 && five.takes[1].sample->frames() == static_cast<size_t>(0.2 * kSampleRate));
   if (s) {
      const SamplePtr t = tone(0.3);
      float err = 0;
      for (size_t i = 0; i < s->frames(); ++i)
         err = std::max(err, std::fabs(s->left[i] - t->left[i]));
      JCLAP_CHECK(err < 1e-4f);
   }
   JCLAP_CHECK(!host->padSample(0));
   JCLAP_CHECK_EQ(h.paramValue(padParam(5, kPitch)), -5.0);
   JCLAP_CHECK_EQ(h.paramValue(kBoom), 0.4);
   JCLAP_CHECK(host->gen().prompt == "a saved prompt" && host->gen().role == kClap);
   JCLAP_CHECK(host->gen().kitMode && host->gen().kitLayout == 2 && host->gen().kitTakes == 4);
   JCLAP_CHECK(host->kitName() == "My Kit");
   // A truncated state is refused.
   Buffer bad;
   bad.bytes.assign(state.bytes.begin(), state.bytes.begin() + state.bytes.size() / 2);
   JCLAP_CHECK(!st->load(h.plugin(), &bad.in));

   // Swapping pads moves the sounds and their params.
   host->setParam(padParam(5, kLevel), -6.0);
   host->swapPads(5, 9);
   JCLAP_CHECK(!host->padSample(5) && host->padSample(9));
   JCLAP_CHECK_EQ(host->paramValue(padParam(9, kLevel)), -6.0);
   JCLAP_CHECK_EQ(host->paramValue(padParam(5, kLevel)), 0.0);
   JCLAP_CHECK_EQ(host->paramValue(padParam(9, kPitch)), -5.0);
}

void testTreatment() {
   Sample s;
   // 0.1 s of silence, then a tone at half scale, then silence.
   const size_t pre = 4410;
   for (size_t i = 0; i < 4410 * 4; ++i) {
      const float v = i >= pre && i < pre * 3 ? 0.5f * static_cast<float>(std::sin(i * 0.05)) : 0.0f;
      s.left.push_back(v);
      s.right.push_back(v);
   }
   Treatment t;
   t.normalize = false;
   t.trimSilence = true;
   Sample out = treat(s, t);
   // The silence is gone, with 2 ms kept before the start.
   JCLAP_CHECK(out.frames() > pre * 2 && out.frames() < pre * 2 + 200);
   t.normalize = true;
   out = treat(s, t);
   JCLAP_CHECK(std::fabs(peakOf(out) - 0.966f) < 0.01f);
   // Start and end as fractions.
   t.trimSilence = false;
   t.start = 0.25;
   t.end = 0.5;
   out = treat(s, t);
   JCLAP_CHECK_EQ(out.frames(), pre);
   // A fade ends at zero.
   t.fadeMs = 20;
   out = treat(s, t);
   JCLAP_CHECK(std::fabs(out.left.back()) < 1e-3f);
   // A fade in starts at zero and rises over its length.
   t.fadeInMs = 20;
   out = treat(s, t);
   const size_t in = static_cast<size_t>(0.02 * kSampleRate);
   JCLAP_CHECK(out.left.front() == 0.0f);
   float early = 0.0f, late = 0.0f;
   for (size_t i = 0; i < in / 8; ++i)
      early = std::max(early, std::fabs(out.left[i]));
   for (size_t i = in; i < in + in / 8; ++i)
      late = std::max(late, std::fabs(out.left[i]));
   JCLAP_CHECK(early < 0.3f * late);
   // Reverse.
   t = Treatment{};
   t.normalize = t.trimSilence = false;
   t.reverse = true;
   out = treat(s, t);
   JCLAP_CHECK(out.left.front() == s.left.back() && out.left.back() == s.left.front());

   // A kit's sounds survive writing and reading, every take with its settings.
   PadSounds pads[16];
   pads[3] = PadSounds::of(tone(0.1, kSnare));
   pads[5].mode = kTakeCycle;
   for (int i = 0; i < 3; ++i) {
      Take k{tone(0.1 + 0.05 * i, kHat)};
      k.start = 0.1 * i;
      k.end = 0.9;
      k.fadeInMs = 5 + i;
      k.fadeOutMs = 30;
      k.levelDb = -3.0 * i;
      k.tune = i - 1.0;
      k.lowCutHz = 200;
      k.highCutHz = 8000;
      pads[5].takes.push_back(k);
   }
   const std::string bytes = writeKit(pads);
   PadSounds back[16];
   JCLAP_CHECK(readKit(bytes, back));
   JCLAP_CHECK(back[3].count() == 1 && back[3].first()->frames() == pads[3].first()->frames() &&
               back[3].first()->role == kSnare && back[0].empty());
   JCLAP_CHECK(back[5].count() == 3 && back[5].mode == kTakeCycle);
   for (int i = 0; i < 3; ++i) {
      const Take &a = pads[5].takes[i], &b = back[5].takes[i];
      JCLAP_CHECK(b.sample->frames() == a.sample->frames() && b.start == a.start && b.end == a.end &&
                  b.fadeInMs == a.fadeInMs && b.fadeOutMs == a.fadeOutMs && b.levelDb == a.levelDb &&
                  b.tune == a.tune && b.lowCutHz == a.lowCutHz && b.highCutHz == a.highCutHz);
   }
   JCLAP_CHECK(!readKit(bytes.substr(0, bytes.size() - 10), back));
   // Kits saved before takes ("JKS1": a 0 or 1 and one sample a pad) still read, as one take each.
   {
      Writer w;
      w.bytes = "JKS1";
      for (int i = 0; i < 16; ++i) {
         w.u8(i == 2 ? 1 : 0);
         if (i == 2)
            w.sample(*tone(0.2, kKick));
      }
      PadSounds old[16];
      JCLAP_CHECK(readKit(w.bytes, old));
      JCLAP_CHECK(old[2].count() == 1 && old[2].first()->role == kKick && old[2].takes[0].end == 1.0 &&
                  old[2].mode == kTakeRandom && old[0].empty());
   }
   // Settings out of range are brought into it.
   Take wild{tone(0.1, kHat)};
   wild.start = 0.9;
   wild.end = 0.2;
   wild.levelDb = 99;
   wild.tune = std::nan("");
   const Take tame = wild.clamped();
   JCLAP_CHECK(tame.start == 0.2 && tame.end == 0.9 && tame.levelDb == 12.0 && tame.tune == 0.0);

   // Which take a hit plays.
   PadSounds four;
   for (int i = 0; i < 4; ++i)
      four.takes.push_back(Take{tone(0.1, kSnare)});
   // RANDOM: never the same one twice running, and every take comes up.
   std::set<int> seen;
   int last = -1;
   for (uint32_t r = 1; r < 200; ++r) {
      const int k = pickTake(four, last, 1.0f, r * 2654435761u);
      JCLAP_CHECK(k >= 0 && k < 4 && k != last);
      seen.insert(k);
      last = k;
   }
   JCLAP_CHECK_EQ(static_cast<int>(seen.size()), 4);
   // CYCLE: in order, round and round.
   four.mode = kTakeCycle;
   JCLAP_CHECK(pickTake(four, -1, 1.0f, 7) == 0 && pickTake(four, 0, 1.0f, 7) == 1 && pickTake(four, 3, 1.0f, 7) == 0);
   // VELOCITY: soft to hard.
   four.mode = kTakeVelocity;
   JCLAP_CHECK(pickTake(four, -1, 0.1f, 7) == 0 && pickTake(four, -1, 0.4f, 7) == 1 &&
               pickTake(four, -1, 0.6f, 7) == 2 && pickTake(four, -1, 1.0f, 7) == 3);
   // One take is always take 0.
   JCLAP_CHECK(pickTake(PadSounds::of(tone(0.1, kSnare)), 0, 0.5f, 99) == 0);
}

} // namespace

int main() {
   testParamText();
   testPlaying();
   testTakes();
   testChokeAndSends();
   testMaster();
   testState();
   testTreatment();
   return jclap::core::testResult();
}
