// Drives JBreath the way a host does: CLAP notes and expressions, MPE MIDI and a breath controller,
// the mono modes, voice stealing, state and params, at several sample rates.

#include <jclap/core/test_host.h>

#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "editor.h"
#include "output.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jbreath;

namespace {

constexpr uint32_t kBlock = 512;
constexpr const char *kId = "com.jeffmhopkins.jclap.jbreath";

struct Run {
   jclap::core::TestHost &host;
   double rate = 48000.0;
   std::vector<float> left, right;
   std::vector<clap_event_note_t> noteEnds;

   // Runs `frames` frames in blocks, keeping both channels and every note end.
   void operator()(uint32_t frames) {
      left.clear();
      right.clear();
      float outL[kBlock], outR[kBlock];
      float *out[2] = {outL, outR};
      while (frames > 0) {
         const uint32_t n = std::min(frames, kBlock);
         const auto result = host.process(n, nullptr, out);
         noteEnds.insert(noteEnds.end(), result.noteEnds.begin(), result.noteEnds.end());
         left.insert(left.end(), outL, outL + n);
         right.insert(right.end(), outR, outR + n);
         frames -= n;
      }
   }
   void seconds(double s) { (*this)(static_cast<uint32_t>(s * rate)); }

   double rms() const {
      double sum = 0.0;
      for (float s : left)
         sum += s * s;
      return left.empty() ? 0.0 : std::sqrt(sum / left.size());
   }
   bool finite() const {
      return std::all_of(left.begin(), left.end(), [](float v) { return std::isfinite(v); });
   }

   // The pitch near `hz`, by normalised autocorrelation over the last 0.2 s.
   double pitch(double hz) const {
      const size_t n = std::min(left.size(), static_cast<size_t>(0.2 * rate));
      const float *x = left.data() + left.size() - n;
      const int lo = static_cast<int>(rate / (hz * 1.3)), hi = static_cast<int>(rate / (hz / 1.3));
      const int m = static_cast<int>(n) - hi - 2;
      if (m <= 0)
         return 0.0;
      std::vector<double> r(hi + 2, 0.0);
      int best = lo;
      for (int L = lo - 1; L <= hi + 1; ++L) {
         double s = 0, e0 = 0, e1 = 0;
         for (int i = 0; i < m; ++i) {
            s += x[i] * x[i + L];
            e0 += x[i] * x[i];
            e1 += x[i + L] * x[i + L];
         }
         r[L] = s / std::sqrt(e0 * e1 + 1e-20);
         if (L >= lo && L <= hi && r[L] > r[best])
            best = L;
      }
      const double a = r[best - 1], b = r[best], c = r[best + 1], den = a - 2 * b + c;
      return rate / (best + (den != 0 ? 0.5 * (a - c) / den : 0.0));
   }
};

double cents(double got, double want) { return got > 0 ? 1200.0 * std::log2(got / want) : 9999.0; }
double hzOf(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

int sounding(EditorHost &panel) {
   std::vector<EditorHost::Voice> v;
   panel.voices(v);
   return static_cast<int>(std::count_if(v.begin(), v.end(), [](const EditorHost::Voice &x) { return x.note >= 0; }));
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

// The voices' own sound: no room, and every note in the middle (the output stage has its own test).
void dry(jclap::core::TestHost &h) {
   h.setParam(kRoom, 0.0);
   h.setParam(kSpread, 0.0);
}

double energy(const std::vector<float> &x) {
   double e = 0.0;
   for (float v : x)
      e += v * v;
   return e;
}

// The voice playing `key`'s sources now.
std::array<float, kModSourceCount> sourcesOf(EditorHost &panel, int key) {
   std::vector<EditorHost::Voice> v;
   panel.voices(v);
   for (const auto &x : v)
      if (x.note == key && !x.releasing)
         return x.sources;
   return {};
}

// The breath's sources span every instrument the same way, and the hands' read MPE as it's played:
// Loop runs 0..1 from the lightest breath to full on every instrument, Fold reaches 1 at full
// breath (the trumpet's too), Knot isn't stuck at full, Fray tells a steady tone from a growl,
// Glide reads a bend of two semitones as all of it whatever the bend range, Shake reads a finger's
// vibrato, Push a pressure rising, and a quick dip in a held note's pressure tongues it again.
void sources() {
   const auto host = [](jclap::core::TestHost &h) {
      dry(h);
      h.setParam(kVibDepth, 0.0);
      h.setParam(kHumanize, 0.0);
   };
   for (int model = 0; model < 6; ++model) {
      jclap::core::TestHost h(clap_entry, kId);
      EditorHost &panel = *h.object<EditorHost>();
      Run run{h};
      host(h);
      h.setParam(kModel, model);
      h.note(CLAP_EVENT_NOTE_ON, 64, 0.6, 1, 1);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 64, 0.06, 1, 1);
      run.seconds(0.8);
      const auto soft = sourcesOf(panel, 64);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 64, 1.0, 1, 1);
      run.seconds(0.8);
      const auto loud = sourcesOf(panel, 64);
      int count = 0;
      std::printf("%s: Loop %.2f..%.2f, Fold %.2f..%.2f, Knot %.2f..%.2f, Fray %.2f..%.2f\n", choiceNames(kModel, count)[model],
                  soft[kSrcLoop], loud[kSrcLoop], soft[kSrcFold], loud[kSrcFold], soft[kSrcKnot], loud[kSrcKnot],
                  soft[kSrcFray], loud[kSrcFray]);
      JCLAP_CHECK(soft[kSrcLoop] < 0.35 && loud[kSrcLoop] > 0.9);
      JCLAP_CHECK(soft[kSrcFold] < 0.05 && loud[kSrcFold] > 0.8);
      JCLAP_CHECK(soft[kSrcKnot] < 0.95);
      JCLAP_CHECK(soft[kSrcFray] >= 0.0f && loud[kSrcFray] <= 1.0f);
   }
   {
      jclap::core::TestHost h(clap_entry, kId);
      EditorHost &panel = *h.object<EditorHost>();
      Run run{h};
      host(h);
      h.setParam(kModel, 2); // the sax
      // A steady tone is hardly frayed; a growl is.
      h.note(CLAP_EVENT_NOTE_ON, 62, 0.6, 1, 1);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, 0.6, 1, 1);
      run.seconds(1.0);
      const float steady = sourcesOf(panel, 62)[kSrcFray];
      h.setParam(kGrowl, 1.0);
      run.seconds(0.8);
      const float growl = sourcesOf(panel, 62)[kSrcFray];
      h.setParam(kGrowl, 0.0);
      std::printf("Fray: steady %.2f, growled %.2f\n", steady, growl);
      JCLAP_CHECK(steady < 0.2 && growl > steady + 0.2);
      // Glide: two semitones up is all of it (with MPE's 48-semitone range); Shake: still, nothing.
      h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, 62, 2.0, 1, 1);
      run.seconds(1.0);
      auto src = sourcesOf(panel, 62);
      JCLAP_CHECK(src[kSrcGlide] > 0.95f);
      JCLAP_CHECK(src[kSrcShake] < 0.05f);
      // A finger's vibrato, a third of a semitone either way at 5.5 Hz.
      float shake = 0.0f;
      for (int i = 0; i < 160; ++i) {
         h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, 62, 2.0 + 0.33 * std::sin(2.0 * 3.14159265 * 5.5 * i * 0.005), 1, 1);
         run.seconds(0.005);
         shake = std::max(shake, sourcesOf(panel, 62)[kSrcShake]);
      }
      std::printf("Glide at +2 st: %.2f, Shake with a finger's vibrato: %.2f\n", src[kSrcGlide], shake);
      JCLAP_CHECK(shake > 0.5f);
      // Push: none held steady, most of it pressing in.
      run.seconds(0.6);
      JCLAP_CHECK(sourcesOf(panel, 62)[kSrcPush] < 0.05f);
      float push = 0.0f;
      for (int i = 0; i < 10; ++i) {
         h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, 0.4 + 0.06 * i, 1, 1);
         run.seconds(0.01);
         push = std::max(push, sourcesOf(panel, 62)[kSrcPush]);
      }
      std::printf("Push pressing in: %.2f\n", push);
      JCLAP_CHECK(push > 0.4f);
      // Re-tongue: a quick dip and back tongues the note again (its strike becomes how hard it
      // came back); a slow diminuendo and back doesn't.
      run.seconds(0.5);
      const auto dip = [&](double low, double each, int steps) {
         for (int i = 1; i <= steps; ++i) {
            h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, 0.9 + (low - 0.9) * i / steps, 1, 1);
            run.seconds(each);
         }
         for (int i = 1; i <= steps; ++i) {
            h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, low + (0.9 - low) * i / steps, 1, 1);
            run.seconds(each);
         }
         run.seconds(0.1);
         return sourcesOf(panel, 62)[kSrcStrike];
      };
      const float slow = dip(0.2, 0.08, 10);
      const float quick = dip(0.15, 0.006, 5);
      std::printf("Strike after a slow dip %.2f, after a quick one %.2f\n", slow, quick);
      JCLAP_CHECK(std::fabs(slow - 0.6f) < 0.01f);
      JCLAP_CHECK(std::fabs(quick - 0.6f) > 0.05f);
      h.setParam(kRetongue, 0.0);
      run.seconds(0.3);
      const float before = sourcesOf(panel, 62)[kSrcStrike];
      JCLAP_CHECK_EQ(dip(0.15, 0.006, 5), before);
      JCLAP_CHECK(run.finite());
   }
   {
      // Tongue Force from the finger: the strike is how hard the pressure arrives, not the velocity.
      jclap::core::TestHost h(clap_entry, kId);
      EditorHost &panel = *h.object<EditorHost>();
      Run run{h};
      host(h);
      h.setParam(kTongueForce, 1);
      h.note(CLAP_EVENT_NOTE_ON, 62, 0.1, 1, 1);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, 0.45, 1, 1);
      run.seconds(0.3);
      const float strike = sourcesOf(panel, 62)[kSrcStrike];
      std::printf("Tongue Force Press: strike %.2f from pressure 0.45\n", strike);
      JCLAP_CHECK(std::fabs(strike - 0.75f) < 0.02f);
      JCLAP_CHECK(run.rms() > 0.01);
   }
}

// Every instrument plays a held note in tune and at a sensible level, at this rate.
void playsInTune(double rate) {
   jclap::core::TestHost h(clap_entry, kId, rate);
   Run run{h, rate};
   dry(h);
   for (int m = 0; m < 6; ++m) {
      h.setParam(kModel, m);
      h.setParam(kVibDepth, 0.0);
      const int key = m == 0 ? 72 : m == 2 ? 55 : 64;
      h.note(CLAP_EVENT_NOTE_ON, key, 0.75, 100 + m);
      run.seconds(0.6);
      const double c = cents(run.pitch(hzOf(key)), hzOf(key));
      const double db = 20.0 * std::log10(run.rms() + 1e-12);
      std::printf("%.0f Hz, %s: %+.1f cents, %.1f dB RMS\n", rate, kModelNames[m], c, db);
      JCLAP_CHECK(std::fabs(c) < 10.0);
      JCLAP_CHECK(db > -32.0 && db < -10.0);
      JCLAP_CHECK(run.finite());
      JCLAP_CHECK(run.left == run.right);
      h.note(CLAP_EVENT_NOTE_OFF, key, 0.0, 100 + m);
      run.seconds(1.0);
      JCLAP_CHECK(run.left.back() == 0.0f);
   }
}

// Hosts can run at any rate, down to 1 kHz (clap-validator tries 1234.57 Hz) and up to 768 kHz:
// every instrument, the Hybrid, the delay and the room stay finite with notes and audio coming in.
void oddRatesStayFinite() {
   for (double rate : {1000.0, 1234.57, 8000.0, 22050.0, 768000.0}) {
      jclap::core::TestHost h(clap_entry, kId, rate);
      h.setParam(kDelayOn, 1.0);
      h.setParam(kHybridMix, 0.5);
      h.setParam(kMute, 2.0);
      std::vector<float> inL(256), inR(256), outL(256), outR(256);
      float *in[2] = {inL.data(), inR.data()}, *out[2] = {outL.data(), outR.data()};
      uint32_t seed = 1;
      bool finite = true;
      for (int m = 0; m < 6; ++m) {
         h.setParam(kModel, m);
         for (int key : {24, 60, 108}) {
            h.note(CLAP_EVENT_NOTE_ON, key, 0.9, key);
            for (int b = 0; b < 8; ++b) {
               for (int i = 0; i < 256; ++i) {
                  seed = seed * 1664525u + 1013904223u;
                  inL[i] = inR[i] = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
               }
               h.process(256, in, out);
               for (int i = 0; i < 256; ++i)
                  finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
            }
            h.note(CLAP_EVENT_NOTE_OFF, key, 0.0, key);
         }
      }
      if (!finite)
         std::printf("not finite at %.2f Hz\n", rate);
      JCLAP_CHECK(finite);
   }
}

// ---- A LinnStrument in MPE mode ----
//
// Each touch is a note on its own channel: its bend and Y (CC 74) just before the note-on, a
// velocity from how fast the pad went down, then channel pressure (Z) rising from 0 as the finger
// presses in, updated every 1 to 5 ms in 7 bits, and when the finger lifts, Z falling back to 0
// before the note-off. Rendered in 1 ms blocks, so every event lands where it would.

constexpr int kMs = 48; // samples per millisecond at 48 kHz

struct Touch {
   int key = 62, channel = 1;
   double velocity = 0.6, pressure = 0.4, rampMs = 10.0;
   int onMs = 20, pressLeadMs = 1, holdMs = 450, liftMs = 30; // Z starts pressLeadMs after the note-on
};

std::vector<float> playTouches(jclap::core::TestHost &h, const std::vector<Touch> &touches, int totalMs) {
   std::vector<float> y;
   std::vector<int> nextZ(touches.size(), 0), lastZ(touches.size(), -1);
   uint32_t rng = 7;
   float l[kMs], r[kMs];
   float *out[2] = {l, r};
   for (int ms = 0; ms < totalMs; ++ms) {
      for (size_t i = 0; i < touches.size(); ++i) {
         const Touch &t = touches[i];
         const int ch = t.channel, zFrom = t.onMs + t.pressLeadMs, liftAt = t.onMs + t.holdMs, offAt = liftAt + t.liftMs;
         if (ms == t.onMs - 1) {
            h.midi(static_cast<uint8_t>(0xE0 | ch), 0, 64);
            h.midi(static_cast<uint8_t>(0xB0 | ch), 74, 64);
         }
         if (ms == t.onMs)
            h.midi(static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(t.key),
                   static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(t.velocity * 127)), 1, 127)));
         if (ms >= zFrom && ms <= offAt && ms >= nextZ[i]) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            nextZ[i] = ms + 1 + static_cast<int>(rng % 5);
            // Pressed in quickly, then settling; and at the lift, back to nothing.
            const double x = std::min(1.0, (ms - zFrom + 1) / t.rampMs);
            double z = t.pressure * (1.0 - (1.0 - x) * (1.0 - x));
            if (ms > liftAt)
               z = t.pressure * std::max(0.0, 1.0 - static_cast<double>(ms - liftAt) / t.liftMs);
            const int zi = static_cast<int>(std::lround(std::clamp(z, 0.0, 1.0) * 127));
            if (zi != lastZ[i]) {
               lastZ[i] = zi;
               h.midi(static_cast<uint8_t>(0xD0 | ch), static_cast<uint8_t>(zi), 0);
            }
         }
         if (ms == offAt)
            h.midi(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(t.key), 50);
      }
      h.process(kMs, nullptr, out);
      y.insert(y.end(), l, l + kMs);
   }
   return y;
}

double rmsOf(const std::vector<float> &y, size_t a, size_t b) {
   double s = 0.0;
   b = std::min(b, y.size());
   for (size_t i = a; i < b; ++i)
      s += y[i] * y[i];
   return b > a ? std::sqrt(s / (b - a)) : 0.0;
}
// The second difference's RMS: the sound's top end, where a click shows.
double highsOf(const std::vector<float> &y, size_t a, size_t b) {
   double s = 0.0;
   a = std::max<size_t>(a, 2);
   b = std::min(b, y.size());
   for (size_t i = a; i < b; ++i) {
      const double d = y[i] - 2.0 * y[i - 1] + y[i - 2];
      s += d * d;
   }
   return b > a ? std::sqrt(s / (b - a)) : 0.0;
}
// The worst click in [a, b): a millisecond's highs over those of the few milliseconds either side
// of it (at least a twentieth of the steady note's), so a step stands out however quiet it is.
double worstClick(const std::vector<float> &y, size_t a, size_t b, double floor) {
   double worst = 0.0;
   for (size_t i = a + 6 * kMs; i + 7 * kMs < b; i += kMs / 2) {
      const double around = 0.5 * (highsOf(y, i - 6 * kMs, i - kMs) + highsOf(y, i + 2 * kMs, i + 7 * kMs));
      worst = std::max(worst, highsOf(y, i, i + kMs) / std::max(around, floor));
   }
   return worst;
}

struct Onset {
   double t90 = 1e9, overshoot = 0.0, burstDb = 0.0, click = 0.0;
   bool speaks = false;
};

// One touch on `model`, measured: when it reaches 90% of its steady level (5 ms windows), how far
// its start overshoots that, how far the start's highs rise over the steady note's (a burst of
// velocity, or a step, would be far above), the worst click from the note-on to the end of the
// tail, and whether the note speaks at all.
Onset touch(const std::vector<std::pair<clap_id, double>> &params, const Touch &t) {
   jclap::core::TestHost h(clap_entry, kId);
   dry(h);
   h.setParam(kVibDepth, 0.0);
   for (const auto &[id, v] : params)
      h.setParam(id, v);
   const std::vector<float> y = playTouches(h, {t}, t.onMs + t.holdMs + t.liftMs + 700);
   const size_t on = static_cast<size_t>(t.onMs) * kMs, a = on + 300 * kMs, b = on + 440 * kMs;
   Onset o;
   const double steady = rmsOf(y, a, b);
   o.speaks = steady > 0.002;
   double highs = 0.0;
   for (size_t i = a; i + kMs < b; i += kMs)
      highs = std::max(highs, highsOf(y, i, i + kMs));
   double peak = 0.0, burst = 0.0;
   for (size_t i = on - 3 * kMs; i + 5 * kMs < a; i += kMs / 2) {
      const double e = rmsOf(y, i, i + 5 * kMs);
      if (o.t90 > 1e8 && e >= 0.9 * steady)
         o.t90 = (static_cast<double>(i) + 2.5 * kMs - on) / kMs;
      if (i < on + 250 * kMs)
         peak = std::max(peak, e);
      burst = std::max(burst, highsOf(y, i, i + kMs));
   }
   o.overshoot = peak / (steady + 1e-12);
   o.burstDb = 20.0 * std::log10(burst / (highs + 1e-12) + 1e-12);
   o.click = worstClick(y, on - 10 * kMs, y.size(), 0.05 * highs);
   return o;
}

// Every instrument (and the Hybrid oscillator) played from a LinnStrument: soft to hard strikes,
// quick to slow presses, light to full pressure, the note-on first or the pressure first. Each
// speaks, starts without a click or a burst, never starts much louder than it settles (a hard
// strike on light pressure is a tongued accent, not the velocity's level falling to the
// pressure's), lets go cleanly, and a harder strike never speaks more slowly.
void linnStrument() {
   const std::vector<std::pair<clap_id, double>> patches[] = {
      {{kModel, 0}}, {{kModel, 1}}, {{kModel, 2}}, {{kModel, 3}}, {{kModel, 4}}, {{kModel, 2}, {kHybridMix, 1.0}}};
   const int keys[] = {72, 62, 61, 69, 65, 61};
   const char *names[] = {"Flute", "Clarinet", "Sax", "Oboe", "Trumpet", "Hybrid"};
   for (int m = 0; m < 6; ++m) {
      double lastT90 = 0.0;
      for (double velocity : {0.1, 0.6, 1.0}) {
         std::vector<double> t90s;
         for (double ramp : {2.0, 40.0})
            for (double pressure : {0.1, 1.0})
               for (int lead : {1, -3}) {
                  Touch t;
                  t.key = keys[m];
                  t.velocity = velocity;
                  t.pressure = pressure;
                  t.rampMs = ramp;
                  t.pressLeadMs = lead;
                  const Onset o = touch(patches[m], t);
                  t90s.push_back(o.t90);
                  const bool ok = o.speaks && o.click < 7.0 && o.burstDb < 10.0 && o.overshoot < 1.6;
                  if (!ok)
                     std::printf("LinnStrument %s, velocity %.1f, %.0f ms to %.1f%s: %s, click %.1f, burst %+.1f dB, "
                                 "start %.2f x\n",
                                 names[m], velocity, ramp, pressure, lead < 0 ? " (pressure first)" : "",
                                 o.speaks ? "speaks" : "silent", o.click, o.burstDb, o.overshoot);
                  JCLAP_CHECK(ok);
               }
         std::sort(t90s.begin(), t90s.end());
         const double median = 0.5 * (t90s[t90s.size() / 2 - 1] + t90s[t90s.size() / 2]);
         if (lastT90 > 0.0 && median > 1.1 * lastT90 + 5.0)
            std::printf("LinnStrument %s: velocity %.1f speaks in %.0f ms, slower than a softer strike's %.0f ms\n",
                        names[m], velocity, median, lastT90);
         JCLAP_CHECK(lastT90 == 0.0 || median <= 1.1 * lastT90 + 5.0);
         lastT90 = median;
      }

      // Quick repeated taps on one pad (on the LinnStrument, round-robin channels), each lifted
      // 15 ms before the next: every one tongued cleanly, none cutting the last one's tail.
      jclap::core::TestHost h(clap_entry, kId);
      dry(h);
      h.setParam(kVibDepth, 0.0);
      for (const auto &[id, v] : patches[m])
         h.setParam(id, v);
      std::vector<Touch> taps;
      for (int i = 0; i < 6; ++i) {
         Touch t;
         t.key = keys[m];
         t.channel = 1 + i % 3;
         t.velocity = 0.4 + 0.1 * (i % 4);
         t.pressure = 0.5;
         t.rampMs = 6.0;
         t.onMs = 20 + i * 75;
         t.holdMs = 50;
         t.liftMs = 10;
         taps.push_back(t);
      }
      const std::vector<float> y = playTouches(h, taps, 20 + 6 * 75 + 800);
      double highs = 0.0;
      for (size_t i = 45 * kMs; i < 70 * kMs; i += kMs)
         highs = std::max(highs, highsOf(y, i, i + kMs));
      const double click = worstClick(y, 10 * kMs, y.size(), 0.05 * highs);
      if (click >= 7.0)
         std::printf("LinnStrument %s, repeated taps: click %.1f\n", names[m], click);
      JCLAP_CHECK(click < 7.0);
   }
}

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, kId);
   EditorHost &panel = *h.object<EditorHost>();
   Run run{h};
   dry(h);

   // Silent until a note arrives.
   run(4800);
   JCLAP_CHECK_EQ(run.rms(), 0.0);

   // The default clarinet, blown by velocity until pressure arrives.
   h.setParam(kVibDepth, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 7);
   run.seconds(0.6);
   JCLAP_CHECK(run.rms() > 0.02);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(62)), hzOf(62))) < 6.0);
   JCLAP_CHECK_EQ(sounding(panel), 1);

   // A CLAP tuning expression bends the bore: up two semitones.
   h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, -1, 2.0, 7);
   run.seconds(0.5);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(64)), hzOf(64))) < 10.0);
   h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, -1, 0.0, 7);

   // Pressure takes over from velocity: blown softly, the note is quieter.
   run.seconds(0.3);
   const double loud = run.rms();
   h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, -1, 0.3, 7);
   run.seconds(1.0);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < loud * 0.9);

   // Let go: the note dies away and the host hears it ended.
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 7);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   JCLAP_CHECK(run.noteEnds.size() == 1 && run.noteEnds[0].note_id == 7);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   run.noteEnds.clear();

   // Breath from pressure only: silent until the controller blows.
   h.setParam(kBreathSource, kSourcePressure);
   h.midi(0x91, 60, 100);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   // A breath controller (CC 2) on the note's channel blows it.
   h.midi(0xB1, 2, 100);
   run.seconds(0.5);
   JCLAP_CHECK(run.rms() > 0.02);
   // MPE: the note channel's bend, at the default 48-semitone range, up one semitone.
   const int bend = 8192 + 8192 / 48;
   h.midi(0xE1, bend & 0x7f, bend >> 7);
   run.seconds(0.5);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(61)), hzOf(61))) < 10.0);
   h.midi(0x81, 60, 0);
   h.midi(0xE1, 0, 64);
   h.midi(0xB1, 2, 0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);

   // Breath from the Breath In parameter (a CV in the host): silent until it rises, then it blows.
   h.setParam(kBreathSource, kSourceInput);
   h.midi(0x91, 60, 100);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.setParam(kBreathIn, 0.8);
   run.seconds(0.5);
   JCLAP_CHECK(run.rms() > 0.02);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(60)), hzOf(60))) < 10.0);
   h.setParam(kBreathIn, 0.0);
   run.seconds(0.5);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.midi(0x81, 60, 0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);

   // Mix: Breath In times each note's own velocity, or its pressure once that arrives. No input is
   // silent whatever the note does.
   h.setParam(kBreathSource, kSourceMix);
   h.setParam(kBreathIn, 1.0);
   h.midi(0x91, 60, 100);
   run.seconds(0.4);
   JCLAP_CHECK(run.rms() > 0.02);
   h.setParam(kBreathIn, 0.0);
   run.seconds(0.5);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.setParam(kBreathIn, 1.0);
   h.midi(0xD1, 90, 0);
   run.seconds(0.6);
   JCLAP_CHECK(run.rms() > 0.02);
   h.midi(0x81, 60, 0);
   h.midi(0xD1, 0, 0);
   h.setParam(kBreathIn, 0.0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   h.setParam(kBreathSource, kSourceAuto);

   // A route: LFO 1 (a 4 Hz sine) onto Fine at a quarter of its range (+-50 cents) bends the
   // pitch up and down; with the route's amount at zero the pitch holds still.
   {
      jclap::core::TestHost mh(clap_entry, kId);
      Run mr{mh};
      mh.setParam(kVibDepth, 0.0);
      mh.setParam(kLfo1Rate, 4.0);
      mh.setParam(routeParam(0, kRouteSource), kSrcLfo1);
      mh.setParam(routeParam(0, kRouteDestination), destinationOf(kFine));
      mh.setParam(routeParam(0, kRouteAmount), 0.25);
      mh.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
      mr.seconds(0.5);
      double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 8; ++i) {
         mr.seconds(0.03125);
         // A short window: the pitch over the last 30 ms.
         const double c = cents(mr.pitch(hzOf(62)), hzOf(62));
         lo = std::min(lo, c);
         hi = std::max(hi, c);
      }
      std::printf("LFO 1 onto Fine: %.0f..%.0f cents\n", lo, hi);
      JCLAP_CHECK(hi - lo > 40.0 && hi - lo < 130.0);
      mh.setParam(routeParam(0, kRouteAmount), 0.0);
      mr.seconds(0.3);
      lo = 1e9, hi = -1e9;
      for (int i = 0; i < 8; ++i) {
         mr.seconds(0.03125);
         const double c = cents(mr.pitch(hzOf(62)), hzOf(62));
         lo = std::min(lo, c);
         hi = std::max(hi, c);
      }
      JCLAP_CHECK(hi - lo < 10.0);

      // Press through Bus 1 onto Breath: a note with no pressure plays from its velocity, and
      // pressure (through the bus) makes it louder.
      mh.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
      mr.seconds(1.0);
      mh.setParam(kBreathSource, kSourceVelocity);
      mh.setParam(routeParam(1, kRouteSource), kSrcPress);
      mh.setParam(routeParam(1, kRouteDestination), kFirstBusDest);
      mh.setParam(routeParam(1, kRouteAmount), 1.0);
      mh.setParam(routeParam(2, kRouteSource), kSrcBus1);
      mh.setParam(routeParam(2, kRouteDestination), destinationOf(kBreath));
      mh.setParam(routeParam(2, kRouteAmount), 0.3);
      mh.setParam(kBreath, 0.8);
      mh.midi(0x91, 60, 70);
      mr.seconds(0.6);
      const double quiet = mr.rms();
      mh.midi(0xD1, 127, 0);
      mr.seconds(0.6);
      const double loud = mr.rms();
      std::printf("Press through Bus 1 onto Breath: %.3f -> %.3f RMS\n", quiet, loud);
      JCLAP_CHECK(quiet > 0.01 && loud > quiet * 1.15);
   }

   run.noteEnds.clear();
   // Eight voices; a ninth note steals one, and the host hears that note ended.
   for (int i = 0; i < 8; ++i)
      h.note(CLAP_EVENT_NOTE_ON, 50 + 2 * i, 0.6, 200 + i);
   run.seconds(0.2);
   JCLAP_CHECK_EQ(sounding(panel), 8);
   JCLAP_CHECK(run.finite());
   h.note(CLAP_EVENT_NOTE_ON, 80, 0.6, 300);
   run.seconds(0.1);
   JCLAP_CHECK_EQ(run.noteEnds.size(), 1u);
   JCLAP_CHECK_EQ(sounding(panel), 8);
   // A choke naming nothing cuts everything at once.
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(256);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   JCLAP_CHECK_EQ(run.noteEnds.size(), 9u);
   // Only the body's short ring (the tube below the holes, and its material's resonance) is left,
   // and it's gone in 150 ms.
   run.seconds(0.15);
   run.seconds(0.1);
   JCLAP_CHECK(run.rms() == 0.0);
   run.noteEnds.clear();

   // Legato: one voice; a second key slurs to it, letting go goes back to the first.
   h.setParam(kVoiceMode, kLegato);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
   run.seconds(0.5);
   h.note(CLAP_EVENT_NOTE_ON, 65, 0.8, 2);
   run.seconds(0.5);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(65)), hzOf(65))) < 8.0);
   // The slur never stopped the air: no gap where the note changed.
   h.note(CLAP_EVENT_NOTE_OFF, 65, 0.0, 2);
   run.seconds(0.5);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(62)), hzOf(62))) < 8.0);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
   run.seconds(1.0);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   run.noteEnds.clear();

   // Mono: the tongue stops the air between notes, so the level dips at the change.
   h.setParam(kVoiceMode, kMono);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
   run.seconds(0.5);
   h.note(CLAP_EVENT_NOTE_ON, 64, 0.8, 2);
   run(static_cast<uint32_t>(0.03 * 48000));
   const double gap = run.rms();
   run.seconds(0.5);
   JCLAP_CHECK(gap < run.rms() * 0.7);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
   h.note(CLAP_EVENT_NOTE_OFF, 64, 0.0, 2);
   run.seconds(1.0);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   h.setParam(kVoiceMode, kPoly);

   // The panel's per-voice samples follow the voice.
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 9);
   run.seconds(0.3);
   std::vector<float> loop(512);
   panel.readVoice(0, loop.data(), loop.size());
   JCLAP_CHECK(std::any_of(loop.begin(), loop.end(), [](float v) { return std::fabs(v) > 1e-3f; }));
   JCLAP_CHECK(panel.outputPeak(0) > 0.0f && panel.outputPeak(1) > 0.0f);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 9);
   run.seconds(1.0);

   // Every param prints and reads back as itself.
   for (const auto &p : kParams) {
      for (double t : {0.0, 0.37, 1.0}) {
         double v = p.min + t * (p.max - p.min);
         if (p.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         char text[64];
         formatParam(p.id, v, text, sizeof(text));
         double back = -1e9;
         JCLAP_CHECK(parseParam(p.id, text, back));
         char again[64];
         formatParam(p.id, back, again, sizeof(again));
         if (std::strcmp(text, again))
            std::printf("param %s: %s -> %s\n", p.name, text, again);
         JCLAP_CHECK(!std::strcmp(text, again));
      }
   }

   // Factory presets: every value in range, names unique.
   const auto &presets = factoryPresets();
   JCLAP_CHECK(presets.size() >= 10);
   for (const auto &p : presets) {
      JCLAP_CHECK_EQ(p.values.size(), static_cast<size_t>(kPresetParamCount));
      for (const auto &[id, v] : p.values)
         JCLAP_CHECK(v >= kParams[id].min && v <= kParams[id].max);
   }
   for (size_t i = 0; i < presets.size(); ++i)
      for (size_t j = i + 1; j < presets.size(); ++j)
         JCLAP_CHECK(presets[i].name != presets[j].name);

   // Every factory preset plays a low and a middle note: sounding, finite, at a usable level, and in
   // tune (a note past the instrument's range plays a whole octave away, so pitch class only).
   for (const auto &p : presets) {
      jclap::core::TestHost ph(clap_entry, kId);
      Run pr{ph};
      for (const auto &[id, v] : p.values)
         ph.setParam(id, v);
      double tune = 0.0;
      for (const auto &[id, v] : p.values)
         tune += id == kTune ? std::round(v) : id == kFine ? v / 100.0 : 0.0;
      for (int key : {50, 62}) {
         ph.note(CLAP_EVENT_NOTE_ON, key, 0.75, key);
         pr.seconds(1.2);
         const double db = 20.0 * std::log10(pr.rms() + 1e-12);
         const double hz = hzOf(key + tune);
         double best = 9999.0;
         for (double oct : {-2.0, -1.0, 0.0, 1.0, 2.0})
            best = std::min(best, std::fabs(cents(pr.pitch(hz * std::pow(2.0, oct)), hz * std::pow(2.0, oct))));
         if (db < -40.0 || db > -6.0 || best > 35.0)
            std::printf("preset %s, key %d: %.1f dB, %.0f cents\n", p.name.c_str(), key, db, best);
         JCLAP_CHECK(pr.finite());
         JCLAP_CHECK(db > -40.0 && db < -6.0);
         JCLAP_CHECK(best < 35.0);
         ph.note(CLAP_EVENT_NOTE_OFF, key, 0.0, key);
         pr.seconds(2.5);
      }
   }

   // Every factory preset lets go of every note: a chord across the whole keyboard, held, released,
   // and after the longest release and a few seconds more no voice is left sounding (a bore that
   // kept ringing on its own once the breath stopped would hold its voice for ever: a hung note).
   for (const auto &p : presets) {
      jclap::core::TestHost ph(clap_entry, kId);
      EditorHost &panel = *ph.object<EditorHost>();
      Run pr{ph};
      for (const auto &[id, v] : p.values)
         ph.setParam(id, v);
      ph.setParam(kVoiceMode, 0.0); // Poly
      const std::vector<int> keys = {28, 34, 40, 46, 52, 58, 64, 70, 76, 82, 88, 96};
      for (int key : keys)
         ph.note(CLAP_EVENT_NOTE_ON, key, 0.75, key);
      pr.seconds(0.8);
      for (int key : keys)
         ph.note(CLAP_EVENT_NOTE_OFF, key, 0.0, key);
      // (A HYBRID pad rings on through its own release, by 99% each Hybrid Release time.)
      double pad = 0.0, mixed = 0.0;
      for (const auto &[id, v] : p.values) {
         pad = id == kHybridRelease ? 2.5 * hybridEnvMs(v) / 1000.0 : pad;
         mixed = id == kHybridMix ? v : mixed;
      }
      pr.seconds(5.0 + (mixed > 0.0 ? pad : 0.0));
      if (sounding(panel) != 0)
         std::printf("preset %s: %d voices still sounding after release\n", p.name.c_str(), sounding(panel));
      JCLAP_CHECK(sounding(panel) == 0);
   }

   // The output stage: Spread puts a low note left of the middle and a high one right; the Room rings
   // on after the voices stop, then empties and is silent.
   {
      jclap::core::TestHost oh(clap_entry, kId);
      EditorHost &op = *oh.object<EditorHost>();
      Run o{oh};
      oh.setParam(kSpread, 1.0);
      oh.setParam(kRoom, 0.0);
      oh.note(CLAP_EVENT_NOTE_ON, 45, 0.8, 1);
      o.seconds(0.5);
      JCLAP_CHECK(energy(o.left) > 2.0 * energy(o.right));
      oh.note(CLAP_EVENT_NOTE_OFF, 45, 0.0, 1);
      o.seconds(1.0);
      JCLAP_CHECK(o.left.back() == 0.0f);
      oh.setParam(kRoom, 0.6);
      oh.setParam(kRoomDecay, 1.0);
      oh.note(CLAP_EVENT_NOTE_ON, 84, 0.8, 2);
      o.seconds(0.5);
      JCLAP_CHECK(energy(o.right) > 2.0 * energy(o.left));
      oh.note(CLAP_EVENT_NOTE_OFF, 84, 0.0, 2);
      o.seconds(0.4);
      JCLAP_CHECK_EQ(sounding(op), 0);
      o.seconds(0.2);
      JCLAP_CHECK(o.rms() > 1e-4);
      o.seconds(2.0);
      JCLAP_CHECK(o.finite());
      JCLAP_CHECK(o.left.back() == 0.0f && o.right.back() == 0.0f);
   }

   // The limiter: a loud poly chord turned up 6 dB stays under full scale with it on, and spends
   // next to no time in the soft clip after it (the limiter does the work, not the clip); a quiet
   // note goes through it untouched.
   {
      auto chord = [&](int mode, double master, double velocity, int notes, float &peak, double &clipped) {
         jclap::core::TestHost ch(clap_entry, kId);
         Run c{ch};
         ch.setParam(kVoiceMode, kPoly);
         ch.setParam(kLimiter, mode);
         ch.setParam(kMaster, master);
         const int keys[] = {48, 55, 60, 64, 67, 72, 76, 79};
         for (int k = 0; k < notes; ++k)
            ch.note(CLAP_EVENT_NOTE_ON, keys[k], velocity, k + 1);
         c.seconds(1.5);
         peak = 0.0f;
         size_t over = 0, counted = 0;
         for (size_t i = 0; i < c.left.size(); ++i) {
            const float a = std::max(std::fabs(c.left[i]), std::fabs(c.right[i]));
            peak = std::max(peak, a);
            if (i > static_cast<size_t>(0.1 * c.rate)) {
               ++counted;
               over += a > 0.8f;
            }
         }
         clipped = counted ? static_cast<double>(over) / counted : 0.0;
         return c.left;
      };
      float peakOff = 0.0f, peakOn = 0.0f, peakClip = 0.0f;
      double clipOff = 0.0, clipOn = 0.0, clipClip = 0.0;
      chord(kLimitOff, 6.0, 1.0, 8, peakOff, clipOff);
      chord(kLimitClip, 6.0, 1.0, 8, peakClip, clipClip);
      chord(kLimitOn, 6.0, 1.0, 8, peakOn, clipOn);
      std::printf("limiter: 8-note chord at +6 dB, peak off %.2f clip %.2f limit %.2f; time over 0.8: off %.1f%% "
                  "clip %.1f%% limit %.2f%%\n",
                  peakOff, peakClip, peakOn, 100 * clipOff, 100 * clipClip, 100 * clipOn);
      JCLAP_CHECK(peakOff > 1.0f);
      JCLAP_CHECK(peakOn < 0.99f && peakClip <= 1.0f);
      JCLAP_CHECK(clipOn < 0.01);
      float qOff = 0.0f, qOn = 0.0f;
      double unused = 0.0;
      const auto quietOff = chord(kLimitOff, -12.0, 0.5, 1, qOff, unused);
      const auto quietOn = chord(kLimitOn, -12.0, 0.5, 1, qOn, unused);
      JCLAP_CHECK(qOn < 0.5f && quietOff == quietOn);
   }

   // Every new control at both ends, on every instrument: still sounding, finite, not too loud, and
   // at the note's pitch (or an octave from it, where a setting overblows it).
   {
      struct Extreme {
         clap_id id;
         double value;
      };
      const Extreme extremes[] = {
         {kStiffness, 0.0}, {kStiffness, 1.0}, {kAperture, 0.0}, {kAperture, 1.0}, {kGrowl, 1.0},
         {kLoss, 0.0}, {kLoss, 0.25}, {kHoleCutoff, 700.0}, {kHoleCutoff, 4000.0}, {kCone, 0.0}, {kCone, 1.0},
         {kBrass, 1.0}, {kRadiate, 0.0}, {kRadiate, 1.0}, {kBarrel, 1.0}, {kWall, 1.0}, {kLength, 1.0},
         {kHoles, 5.0}, {kTube, 7.0}, {kDetune, 25.0}, {kHumanize, 1.0}, {kAir, 1.0},
         {kTongueType, kTongueFlutter}, {kTongueType, kTongueBreath}, {kTongueType, kTongueSoft},
         {kVibType, kVibJaw}, {kVibType, kVibLip}, {kHybridMix, 0.5}, {kHybridMix, 1.0}, {kLimiter, 2.0},
      };
      for (int m = 0; m < 6; ++m)
         for (const Extreme &x : extremes) {
            jclap::core::TestHost eh(clap_entry, kId);
            Run e{eh};
            dry(eh);
            eh.setParam(kModel, m);
            eh.setParam(kVibDepth, 0.0);
            eh.setParam(x.id, x.value);
            const int key = m == 4 ? 62 : 60;
            eh.note(CLAP_EVENT_NOTE_ON, key, 0.75, 1);
            e.seconds(0.8);
            const double db = 20.0 * std::log10(e.rms() + 1e-12);
            double best = 9999.0;
            for (double oct : {-1.0, 0.0, 1.0})
               best = std::min(best, std::fabs(cents(e.pitch(hzOf(key + 12 * oct)), hzOf(key + 12 * oct))));
            const bool ok = e.finite() && db > -40.0 && db < -3.0 && best < 40.0;
            if (!ok)
               std::printf("%s with %s at %g: %.1f dB, %.0f cents\n", kModelNames[m], kParams[x.id].name, x.value, db,
                           best);
            JCLAP_CHECK(ok);
         }
   }

   // State round trip, with the preset name.
   {
      h.setParam(kModel, 4);
      h.setParam(kEdge, 1.7);
      run(64);
      panel.setPresetName("Brass Plume");
      panel.setLyrics("Hello, I'm an alien");
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      Buffer b;
      JCLAP_CHECK(state->save(h.plugin(), &b.out));
      jclap::core::TestHost h2(clap_entry, kId);
      JCLAP_CHECK(state->load(h2.plugin(), &b.in));
      EditorHost &panel2 = *h2.object<EditorHost>();
      JCLAP_CHECK_EQ(panel2.paramValue(kModel), 4.0);
      JCLAP_CHECK_EQ(panel2.paramValue(kEdge), 1.7);
      JCLAP_CHECK(panel2.presetName() == "Brass Plume");
      JCLAP_CHECK(panel2.lyrics() == "Hello, I'm an alien");
      // A state from before the Alien's words (version 1, no words after the name) still loads.
      {
         const uint32_t words = static_cast<uint32_t>(std::string("Hello, I'm an alien").size());
         Buffer old;
         old.bytes.assign(b.bytes.begin(), b.bytes.end() - 4 - words);
         old.bytes[4] = 1;
         jclap::core::TestHost h3(clap_entry, kId);
         JCLAP_CHECK(state->load(h3.plugin(), &old.in));
         EditorHost &panel3 = *h3.object<EditorHost>();
         JCLAP_CHECK_EQ(panel3.paramValue(kEdge), 1.7);
         JCLAP_CHECK(panel3.presetName() == "Brass Plume");
         JCLAP_CHECK(panel3.lyrics().empty());
      }
      // Someone else's state is refused.
      Buffer bad;
      bad.bytes = {'J', 'B', 'S', 'Y', 1, 0, 0, 0};
      JCLAP_CHECK(!state->load(h2.plugin(), &bad.in));
   }

   // The Alien sings typed words, a syllable a note (a chord's notes the same one), and shows
   // which; the words it sings are the typed ones, split as English splits them.
   {
      jclap::core::TestHost ah(clap_entry, kId);
      Run ar{ah};
      EditorHost &ap = *ah.object<EditorHost>();
      ah.setParam(kModel, 5);
      ah.setParam(kAlienSource, 1);
      ap.setLyrics("hello alien");
      ar.seconds(0.05);
      std::string text;
      std::vector<std::pair<int, int>> syllables;
      ap.sungWords(text, syllables);
      JCLAP_CHECK(text == "hello alien");
      JCLAP_CHECK_EQ(syllables.size(), static_cast<size_t>(5)); // hel-lo a-li-en
      JCLAP_CHECK(syllables[0] == std::make_pair(0, 5) && syllables[2] == std::make_pair(6, 11));
      int said[3];
      for (int n = 0; n < 3; ++n) {
         ah.note(CLAP_EVENT_NOTE_ON, 60 + n, 0.8, 60 + n);
         ar.seconds(0.3);
         said[n] = -1;
         for (int slot = 0; slot < kVoiceCount; ++slot) {
            std::vector<EditorHost::Voice> v;
            ap.voices(v);
            if (v[slot].note == 60 + n)
               said[n] = v[slot].saying;
         }
         ah.note(CLAP_EVENT_NOTE_OFF, 60 + n, 0.0, 60 + n);
         ar.seconds(0.3);
      }
      std::printf("Alien sings syllables %d %d %d\n", said[0], said[1], said[2]);
      JCLAP_CHECK(said[0] == 0 && said[1] == 1 && said[2] == 2);
      JCLAP_CHECK(ar.finite());
      // A chord: both notes on the next syllable.
      ah.note(CLAP_EVENT_NOTE_ON, 64, 0.8, 64);
      ah.note(CLAP_EVENT_NOTE_ON, 67, 0.8, 67);
      ar.seconds(0.2);
      std::vector<EditorHost::Voice> v;
      ap.voices(v);
      int chord[2] = {-1, -1}, k = 0;
      for (const auto &voice : v)
         if ((voice.note == 64 || voice.note == 67) && k < 2)
            chord[k++] = voice.saying;
      JCLAP_CHECK(k == 2 && chord[0] == 3 && chord[1] == 3);
   }

   // Every instrument, in tune at the common rates (oversampled 2x at 44.1 and 48 kHz, not at 96).
   playsInTune(44100.0);
   playsInTune(48000.0);
   playsInTune(96000.0);
   playsInTune(192000.0);
   oddRatesStayFinite();

   linnStrument();
   sources();

   return jclap::core::testResult();
}
