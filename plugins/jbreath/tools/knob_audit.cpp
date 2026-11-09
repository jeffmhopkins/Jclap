// Does every sound knob do something you can hear? For each instrument model and each param that
// shapes the sound, a short phrase is played through the whole plugin (CLAP notes, pressure and
// param events, the body, room and limiter after the voices), with the param at its minimum,
// centre and maximum and every other param at its default. The renders are compared with measures
// that follow what a listener hears, each scaled by its just-noticeable difference (JND), and the
// largest is the pair's audibility score: 1 is just noticeable.
//
//    jbreath-knob-audit [--out DIR] [--wav DIR] [--threads N] [--model NAME] [--param NAME] [--features]
//
// writes DIR/knob_audit.csv (one row per param and model) and DIR/knob_audit.md (the summary), and
// prints the summary. With --wav, the min, centre and max renders of every flagged param are saved
// there as 16-bit stereo WAVs. --model and --param (repeatable, case-insensitive substrings) limit
// the run. --features prints each model's default render's measures, note by note.
//
// The phrase, per model (notes inside the instrument's real range, see notesFor):
//    1. a low note, velocity 0.7, pressure 0.5, tongued (the default Hard tongue), held 1.0 s
//    2. a high note, the same
//    3. a soft middle note: velocity 0.25, pressure 0.15
//    4. a loud middle note: velocity 0.95, pressure 0.9
// each released after 1.0 s, 1.6 s apart. A few params only act with another one on, and are
// measured with it on (their "context"): the Hybrid voice's params with Hybrid Mix at 50%, Flutter
// Rate with Flutter at 50%, Unison Detune with 3 voices, the room's params with Room at 40%,
// Vibrato Rate and Type with Vibrato Depth at 15%, the Limiter with Level at +12 dB, and Glide in
// Legato mode, where the high note is slurred from the low one.
//
// The measures, per note, and their JNDs (a difference of one JND scores 1.0):
//    level           RMS, A-weighted RMS, and left and right RMS of the held part   1 dB
//    spectrum        1/3-octave band levels (50 Hz-16 kHz) relative to the total,
//                    RMS difference across bands (spectral shape, level removed)    1.5 dB
//    harmonics       H1..H10 relative to the total harmonic power, RMS difference   2 dB
//    centroid        spectral centroid, relative change                             5 %
//    pitch           f0 by YIN over the held part                                   5 cents
//    onset           time to 90% of the held level from note-on                     20 % (at least 10 ms)
//    overshoot       peak of the attack over the held level (0 if none)             2 dB
//    release         time to fall 20 dB after note-off                              20 % (at least 10 ms)
//    pitch settle    time until the pitch stays within 20 cents of the held pitch   30 % (at least 30 ms)
//    noise           noise-to-harmonic ratio (energy between the harmonics)         3 dB
//    tremolo         amplitude modulation depth, peak to peak                       1 dB
//    vibrato         pitch modulation depth, peak to peak                           6 cents
//    mod rate        rate of the stronger modulation, when both renders have it     10 %
// A pair's score is the largest scaled difference over every measure and note. Per param and
// model, on the largest of its pairs: OK at 3 or more, WEAK from 1 to 3, INAUDIBLE under 1. Per
// param: INAUDIBLE when inaudible on every model, MODEL-SPECIFIC when audible on some only, WEAK
// when audible everywhere but weak on most, OK otherwise.
//
// Also flagged: NON-MONOTONIC (min and max closer to each other than the larger half of the travel
// scores, by 40%), ONE-SIDED (one half of the travel scores under 0.5 while the other scores 2 or
// more), SILENT (a note that sounds at the default is silent or 30 dB quieter: it doesn't speak),
// BROKEN (non-finite output), CLIP (a peak at full scale), LOUD (12 dB or more over the default),
// CLICK (the sharpest second difference in the first 60 ms or the 150 ms after note-off, over the
// held sound's RMS of it, is over 20 dB and 6 dB more than at the default), and for choices, SAME
// (two choices that score under 1 against each other). Level and Master are expected to go silent
// or clip at their ends, so they aren't flagged for it.
//
// Renders are deterministic (each voice slot has a fixed seed), so every difference is the param's.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "params.h"
#include "voice.h"

extern "C" const clap_plugin_entry_t clap_entry;

namespace {

using namespace jbreath;

constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 256;
constexpr const char *kPluginId = "com.jeffmhopkins.jclap.jbreath";

// ---- What to measure ------------------------------------------------------------------------

// The JNDs, as in the header.
namespace jnd {
constexpr double level = 1.0, bands = 1.5, harm = 2.0, centroid = 5.0, cents = 5.0, onset = 0.20, onsetMin = 0.010,
                 overshoot = 2.0, release = 0.20, releaseMin = 0.010, settle = 0.30, settleMin = 0.030, nhr = 3.0,
                 am = 1.0, fm = 6.0, rate = 0.10;
}

// Why a param isn't audited.
const char *skipReason(clap_id id) {
   if (isLookParam(id))
      return "the AIR view's look";
   if (id == kModel)
      return "the instrument itself (every model is audited)";
   if (id >= kLfo1Rate && id < kRouteEnd)
      return "modulation plumbing (LFOs, Swell, buses, routes)";
   switch (id) {
   case kVoiceMode:
      return "MIDI/voice setup (used as Glide's context)";
   case kBreathSource:
      return "MIDI setup: where the breath comes from";
   case kBendRange:
      return "MIDI setup: MPE bend range";
   case kSlide:
      return "MIDI setup: how far MPE slide moves the embouchure (no slide in the phrase)";
   case kBreathIn:
      return "an input to blow from (plumbing), not a sound setting";
   default:
      return nullptr;
   }
}

struct Context {
   std::vector<std::pair<clap_id, double>> set;
   bool legato = false;
   const char *note = "";
};

Context contextFor(clap_id id) {
   Context c;
   switch (id) {
   case kGlide:
      c.set = {{kVoiceMode, kLegato}};
      c.legato = true;
      c.note = "Legato, high note slurred";
      break;
   case kFlutterRate:
      c.set = {{kFlutter, 0.5}};
      c.note = "Flutter 50%";
      break;
   case kUnisonDetune:
      c.set = {{kUnison, 3}};
      c.note = "Unison 3";
      break;
   case kHybridModel:
   case kHybridOctave:
   case kHybridDetune:
   case kHybridBreath:
   case kHybridCutoff:
   case kHybridResonance:
   case kHybridFilterBreath:
   case kHybridFollow:
      c.set = {{kHybridMix, 0.5}};
      c.note = "Hybrid Mix 50%";
      break;
   case kVibRate:
   case kVibType:
   case kVibDelay:
      // The default vibrato (5% breath) is faint: its rate and kind need some to be heard.
      c.set = {{kVibDepth, 0.15}};
      c.note = "Vibrato Depth 15%";
      break;
   case kLimiter:
      // At the default level nothing reaches the limiter; it's there for loud playing.
      c.set = {{kLevel, 12.0}};
      c.note = "Level +12 dB";
      break;
   case kRoomSize:
   case kRoomDecay:
   case kRoomDamp:
   case kRoomPreDelay:
   case kReverbOn:
      c.set = {{kRoom, 0.4}};
      c.note = "Room 40%";
      break;
   case kHybridDecay:
      // Decay falls to the sustain level: at full sustain there's nothing to fall.
      c.set = {{kHybridMix, 0.5}, {kHybridFollow, 0.0}, {kHybridSustain, 0.3}};
      c.note = "Hybrid 50%, Sus 30%";
      break;
   case kHybridAttack:
   case kHybridSustain:
   case kHybridRelease:
   case kHybridFilterEnv:
      // The envelope plays the Hybrid voice: heard with some of it, and most clearly as a pad (Follow 0).
      c.set = {{kHybridMix, 0.5}, {kHybridFollow, 0.0}};
      c.note = "Hybrid 50%, Follow 0";
      break;
   case kDelayMix:
   case kDelayTime:
   case kDelaySync:
   case kDelayFeedback:
   case kDelayTone:
   case kDelaySpread:
      c.set = {{kDelayOn, 1.0}};
      c.note = "Delay On";
      break;
   case kMuteAmount:
      c.set = {{kMute, 1.0}};
      c.note = "Mute Straight";
      break;
   default:
      break;
   }
   return c;
}

// Params meant for some models only (the panel says so): audible elsewhere isn't expected.
std::vector<int> intendedModels(clap_id id) {
   if (id == kCone)
      return {kSax, kOboe, kTrumpet, kAlien};
   if (id == kBrass)
      return {kTrumpet};
   if (id >= kAlienVoice && id <= kAlienThroat)
      return {kAlien};
   return {};
}

bool isChoice(clap_id id) { return (kParams[id].flags & CLAP_PARAM_IS_STEPPED) != 0 && kParams[id].max - kParams[id].min <= 8; }

// The values a param is rendered at: min, centre and max, or every choice.
std::vector<double> valuesFor(clap_id id) {
   const auto &p = kParams[id];
   if (isChoice(id)) {
      std::vector<double> v;
      for (double x = p.min; x <= p.max + 1e-9; x += 1.0)
         v.push_back(x);
      return v;
   }
   double mid = 0.5 * (p.min + p.max);
   if (p.flags & CLAP_PARAM_IS_STEPPED)
      mid = std::round(mid);
   return {p.min, mid, p.max};
}
// Which of those are min, centre and max.
void lowMidHigh(clap_id id, const std::vector<double> &v, int &lo, int &mid, int &hi) {
   lo = 0;
   hi = static_cast<int>(v.size()) - 1;
   if (!isChoice(id)) {
      mid = 1;
      return;
   }
   const double c = std::round(0.5 * (kParams[id].min + kParams[id].max));
   mid = 0;
   for (int i = 0; i < static_cast<int>(v.size()); ++i)
      if (std::fabs(v[i] - c) < 0.5)
         mid = i;
}

// ---- The phrase -----------------------------------------------------------------------------

constexpr int kSegs = 4;
const char *const kSegNames[kSegs] = {"low", "high", "soft", "loud"};
constexpr double kHold = 1.0, kSegGap = 1.6, kTail = 1.2;

struct NoteSpec {
   int key;
   double velocity, pressure, on, off;
};

struct Phrase {
   NoteSpec notes[kSegs];
   double total = 0.0;
};

// Low, high and middle notes (MIDI, sounding pitch) inside each instrument's real range: flute
// C4-C7, clarinet D3-C6, alto sax Db3-Ab5, oboe Bb3-A6, trumpet F#3-C6. A model added later
// takes them from the baked tuning table's range until it's given its own here.
void notesFor(int model, int keys[3]) {
   switch (model) {
   case kFlute: keys[0] = 62, keys[1] = 86, keys[2] = 74; return;
   case kClarinet: keys[0] = 52, keys[1] = 79, keys[2] = 65; return;
   case kSax: keys[0] = 51, keys[1] = 75, keys[2] = 63; return;
   case kOboe: keys[0] = 60, keys[1] = 84, keys[2] = 72; return;
   case kTrumpet: keys[0] = 57, keys[1] = 79, keys[2] = 67; return;
   default: break;
   }
   const TuningTable &t = TuningTable::get();
   const int lo = t.lowest(model), hi = std::max(t.highest(model), lo + 12);
   keys[0] = lo + std::max(2, (hi - lo) / 8);
   keys[1] = lo + static_cast<int>(std::lround(0.7 * (hi - lo)));
   keys[2] = (keys[0] + keys[1]) / 2;
}

Phrase phraseFor(int model, bool legato) {
   int k[3];
   notesFor(model, k);
   Phrase p;
   p.notes[0] = {k[0], 0.7, 0.5, 0.0, kHold};
   p.notes[1] = {k[1], 0.7, 0.5, kSegGap, kSegGap + kHold};
   p.notes[2] = {k[2], 0.25, 0.15, 2 * kSegGap, 2 * kSegGap + kHold};
   p.notes[3] = {k[2], 0.95, 0.9, 3 * kSegGap, 3 * kSegGap + kHold};
   if (legato) {
      // The high note slurs out of the low one: it starts as the low one lets go.
      p.notes[0].off = kHold + 0.01;
      p.notes[1].on = kHold;
      p.notes[1].off = 2 * kHold;
   }
   p.total = 3 * kSegGap + kHold + kTail;
   return p;
}

struct Render {
   std::vector<float> l, r;
};

Render render(int model, const std::vector<std::pair<clap_id, double>> &set, bool legato) {
   jclap::core::TestHost h(clap_entry, kPluginId, kRate);
   h.setParam(kModel, model);
   for (const auto &s : set)
      h.setParam(s.first, s.second);
   const Phrase ph = phraseFor(model, legato);
   struct Ev {
      uint32_t frame;
      int kind; // 0 off, 1 on (with pressure)
      int seg;
   };
   std::vector<Ev> evs;
   for (int i = 0; i < kSegs; ++i) {
      evs.push_back({static_cast<uint32_t>(ph.notes[i].on * kRate), 1, i});
      evs.push_back({static_cast<uint32_t>(ph.notes[i].off * kRate), 0, i});
   }
   std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.frame < b.frame; });
   const uint32_t total = static_cast<uint32_t>(ph.total * kRate);
   Render out;
   out.l.reserve(total);
   out.r.reserve(total);
   float bl[kBlock], br[kBlock];
   float *bufs[2] = {bl, br};
   size_t next = 0;
   for (uint32_t start = 0; start < total; start += kBlock) {
      const uint32_t n = std::min(kBlock, total - start);
      while (next < evs.size() && evs[next].frame < start + n) {
         const Ev &e = evs[next++];
         const NoteSpec &ns = ph.notes[e.seg];
         const uint32_t t = e.frame - std::min(e.frame, start);
         if (e.kind == 1) {
            h.note(CLAP_EVENT_NOTE_ON, static_cast<int16_t>(ns.key), ns.velocity, 100 + e.seg, 0, t);
            h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, static_cast<int16_t>(ns.key), ns.pressure, 100 + e.seg, 0, t);
         } else {
            h.note(CLAP_EVENT_NOTE_OFF, static_cast<int16_t>(ns.key), 0.5, 100 + e.seg, 0, t);
         }
      }
      h.process(n, nullptr, bufs);
      out.l.insert(out.l.end(), bl, bl + n);
      out.r.insert(out.r.end(), br, br + n);
   }
   return out;
}

// ---- Analysis -------------------------------------------------------------------------------

void fft(std::vector<std::complex<double>> &a) {
   const size_t n = a.size();
   for (size_t i = 1, j = 0; i < n; ++i) {
      size_t bit = n >> 1;
      for (; j & bit; bit >>= 1)
         j ^= bit;
      j ^= bit;
      if (i < j)
         std::swap(a[i], a[j]);
   }
   for (size_t len = 2; len <= n; len <<= 1) {
      const double ang = -2 * kPi / static_cast<double>(len);
      const std::complex<double> wl(std::cos(ang), std::sin(ang));
      for (size_t i = 0; i < n; i += len) {
         std::complex<double> w(1);
         for (size_t k = 0; k < len / 2; ++k) {
            const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
            a[i + k] = u + v;
            a[i + k + len / 2] = u - v;
            w *= wl;
         }
      }
   }
}
size_t pow2(size_t n) {
   size_t p = 1;
   while (p < n)
      p <<= 1;
   return p;
}
double db(double power) { return 10.0 * std::log10(power + 1e-20); }

constexpr int kBands = 26; // 1/3 octaves, 50 Hz to 16 kHz
double bandCentre(int b) { return std::pow(10.0, (17 + b) / 10.0); }
double aWeight(double f) {
   const double f2 = f * f;
   const double ra = (12194.0 * 12194.0 * f2 * f2) /
                     ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
   return ra * ra * 1.2589; // +2 dB at 1 kHz, as the standard's normalisation
}

struct Feat {
   bool silent = true, finite = true;
   double rmsDb = -200, aDb = -200, lDb = -200, rDb = -200, peak = 0;
   double bands[kBands] = {};
   double harm[10] = {};
   double centroid = 0, f0 = 0, nhr = 0;
   bool pitched = false;
   double t90 = 0, overshoot = 0, rel20 = 0, settle = 0;
   double am = 0, amRate = 0, fm = 0, fmRate = 0;
   double click = -200; // the largest step at the attack or release over the held sound's, dB
};

// f0 by YIN over x, between 30 Hz and 2.5 kHz; 0 when there is no clear pitch.
double yin(const std::vector<double> &x, bool &pitched) {
   const size_t n = x.size();
   const int minLag = static_cast<int>(kRate / 2500.0), maxLag = std::min(static_cast<int>(kRate / 30.0), static_cast<int>(n / 2));
   const size_t N = pow2(2 * n);
   std::vector<std::complex<double>> a(N);
   for (size_t i = 0; i < n; ++i)
      a[i] = x[i];
   fft(a);
   for (auto &v : a)
      v = std::norm(v);
   fft(a); // the inverse of a real, even spectrum, up to scale and order
   std::vector<double> r(maxLag + 2);
   for (int t = 0; t <= maxLag + 1; ++t)
      r[t] = a[(N - t) % N].real() / N;
   std::vector<double> e(n + 1, 0.0);
   for (size_t i = 0; i < n; ++i)
      e[i + 1] = e[i] + x[i] * x[i];
   // Over a fixed window of the first n - maxLag samples, as YIN's difference function.
   const size_t W = n - maxLag - 1;
   std::vector<double> d(maxLag + 2, 0.0), dn(maxLag + 2, 1.0);
   // r over the window: correct the full autocorrelation's edge (the samples past W) by direct sum
   // would be slow, so use the full-signal form: d(t) = E[0,n-t) + E[t,n) - 2 r(t), normalised by
   // the overlap so long lags aren't favoured.
   (void)W;
   double run = 0.0;
   for (int t = 1; t <= maxLag + 1; ++t) {
      const double ov = static_cast<double>(n - t);
      d[t] = (e[n - t] + (e[n] - e[t]) - 2.0 * r[t]) / ov;
      run += d[t];
      dn[t] = d[t] * t / (run + 1e-30);
   }
   int best = -1;
   for (int t = minLag; t <= maxLag; ++t)
      if (dn[t] < 0.12) {
         while (t + 1 <= maxLag && dn[t + 1] < dn[t])
            ++t;
         best = t;
         break;
      }
   if (best < 0) {
      best = minLag;
      for (int t = minLag; t <= maxLag; ++t)
         if (dn[t] < dn[best])
            best = t;
   }
   pitched = dn[best] < 0.35;
   const double a0 = dn[best - 1], b0 = dn[best], c0 = dn[best + 1], den = a0 - 2 * b0 + c0;
   const double lag = best + (den > 0 ? 0.5 * (a0 - c0) / den : 0.0);
   return kRate / lag;
}

// Normalised autocorrelation's best lag within [lo, hi] for the frame at x[start..start+len).
double frameLag(const std::vector<double> &x, size_t start, int len, int lo, int hi, bool &edge) {
   std::vector<double> r(hi + 2, -1.0);
   int best = lo;
   for (int L = lo - 1; L <= hi + 1; ++L) {
      double s = 0, e0 = 0, e1 = 0;
      const double *a = x.data() + start, *b = a + L;
      for (int i = 0; i < len; ++i) {
         s += a[i] * b[i];
         e0 += a[i] * a[i];
         e1 += b[i] * b[i];
      }
      r[L] = s / std::sqrt(e0 * e1 + 1e-30);
      if (L >= lo && L <= hi && r[L] > r[best])
         best = L;
   }
   edge = best == lo || best == hi || r[best] < 0.5;
   const double a0 = r[best - 1], b0 = r[best], c0 = r[best + 1], den = a0 - 2 * b0 + c0;
   return best + (den < 0 ? 0.5 * (a0 - c0) / den : 0.0);
}

// Peak to peak depth (2 sqrt 2 times the standard deviation, after taking out a straight line)
// and the strongest rate in 1.5..45 Hz, of a track sampled at `fr` Hz.
void modulation(const std::vector<double> &y, double fr, double &depth, double &rate) {
   const size_t n = y.size();
   depth = rate = 0.0;
   if (n < 8)
      return;
   double sx = 0, sy = 0, sxx = 0, sxy = 0;
   for (size_t i = 0; i < n; ++i) {
      sx += i;
      sy += y[i];
      sxx += double(i) * i;
      sxy += i * y[i];
   }
   const double k = (n * sxy - sx * sy) / (n * sxx - sx * sx + 1e-30), c = (sy - k * sx) / n;
   std::vector<double> z(n);
   double v = 0;
   for (size_t i = 0; i < n; ++i) {
      z[i] = y[i] - (c + k * i);
      v += z[i] * z[i];
   }
   depth = 2.0 * std::sqrt(2.0) * std::sqrt(v / n);
   double bestP = 0;
   for (double f = 1.5; f <= std::min(45.0, 0.45 * fr); f += 0.25) {
      std::complex<double> s(0);
      for (size_t i = 0; i < n; ++i)
         s += z[i] * std::polar(1.0, -2 * kPi * f * i / fr);
      if (std::norm(s) > bestP) {
         bestP = std::norm(s);
         rate = f;
      }
   }
}

Feat analyse(const Render &rd, const NoteSpec &ns) {
   Feat F;
   const size_t on = static_cast<size_t>(ns.on * kRate), off = static_cast<size_t>(ns.off * kRate);
   const size_t s0 = on + static_cast<size_t>(0.4 * kRate), s1 = on + static_cast<size_t>(kHold * kRate);
   const size_t total = rd.l.size();
   for (size_t i = on; i < std::min(total, off + static_cast<size_t>(0.5 * kRate)); ++i) {
      if (!std::isfinite(rd.l[i]) || !std::isfinite(rd.r[i]))
         F.finite = false;
      F.peak = std::max<double>(F.peak, std::max(std::fabs(rd.l[i]), std::fabs(rd.r[i])));
   }
   if (!F.finite)
      return F;
   std::vector<double> mid(total);
   for (size_t i = 0; i < total; ++i)
      mid[i] = 0.5 * (rd.l[i] + rd.r[i]);
   double pm = 0, pl = 0, pr = 0;
   for (size_t i = s0; i < s1; ++i) {
      pm += mid[i] * mid[i];
      pl += double(rd.l[i]) * rd.l[i];
      pr += double(rd.r[i]) * rd.r[i];
   }
   const double len = static_cast<double>(s1 - s0);
   F.rmsDb = db(pm / len);
   F.lDb = db(pl / len);
   F.rDb = db(pr / len);
   F.silent = F.rmsDb < -80.0;
   if (F.silent)
      return F;

   // The held part's spectrum.
   const size_t n = s1 - s0, N = pow2(n);
   std::vector<std::complex<double>> a(N);
   double wsum = 0;
   for (size_t i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
      a[i] = mid[s0 + i] * w;
      wsum += w * w;
   }
   fft(a);
   const double binHz = kRate / N;
   std::vector<double> P(N / 2);
   for (size_t k = 0; k < N / 2; ++k)
      P[k] = std::norm(a[k]) / (wsum * N) * 2.0;
   const auto bin = [&](double f) { return std::clamp<size_t>(static_cast<size_t>(f / binHz + 0.5), 1, N / 2 - 1); };
   double tot = 0, aw = 0, cnum = 0, cden = 0;
   for (size_t k = bin(40.0); k <= bin(20000.0); ++k) {
      const double f = k * binHz;
      tot += P[k];
      aw += P[k] * aWeight(f);
      cnum += f * std::sqrt(P[k]);
      cden += std::sqrt(P[k]);
   }
   F.aDb = db(aw);
   F.centroid = cnum / (cden + 1e-30);
   for (int b = 0; b < kBands; ++b) {
      double s = 0;
      const double fc = bandCentre(b);
      for (size_t k = bin(fc * std::pow(2.0, -1.0 / 6)); k < bin(fc * std::pow(2.0, 1.0 / 6)); ++k)
         s += P[k];
      F.bands[b] = std::max(-60.0, db(s) - db(tot));
   }

   std::vector<double> held(mid.begin() + s0, mid.begin() + s1);
   F.f0 = yin(held, F.pitched);
   if (F.pitched) {
      double harmP = 0, hk[10] = {};
      for (int h = 1; h * F.f0 < 12000.0; ++h) {
         double s = 0;
         for (size_t k = bin((h - 0.25) * F.f0); k <= bin((h + 0.25) * F.f0); ++k)
            s += P[k];
         harmP += s;
         if (h <= 10)
            hk[h - 1] = s;
      }
      double band = 0;
      for (size_t k = bin(0.75 * F.f0); k <= bin(12000.0); ++k)
         band += P[k];
      F.nhr = db(std::max(band - harmP, 1e-20)) - db(harmP);
      for (int h = 0; h < 10; ++h)
         F.harm[h] = std::max(-60.0, db(hk[h]) - db(harmP));
   }

   // The envelope: RMS over two periods (at least 5 ms, at most 25 ms), every 2 ms.
   const double period = F.pitched ? kRate / F.f0 : 240.0;
   const int win = static_cast<int>(std::clamp(2.0 * period, 240.0, 1200.0));
   const int hop = 96;
   std::vector<double> env;
   for (size_t i = on; i + win < std::min(total, off + static_cast<size_t>(0.6 * kRate)); i += hop) {
      double s = 0;
      for (int j = 0; j < win; ++j)
         s += mid[i + j] * mid[i + j];
      env.push_back(std::sqrt(s / win));
   }
   const auto at = [&](double t) { return std::min(env.size() - 1, static_cast<size_t>(t * kRate / hop)); };
   std::vector<double> steady(env.begin() + at(0.4), env.begin() + at(kHold - win / kRate));
   std::nth_element(steady.begin(), steady.begin() + steady.size() / 2, steady.end());
   const double level = steady[steady.size() / 2];
   F.t90 = kHold;
   for (size_t i = 0; i < at(kHold); ++i)
      if (env[i] >= 0.9 * level) {
         F.t90 = (i * hop + win / 2) / kRate;
         break;
      }
   double top = 0;
   for (size_t i = 0; i < at(0.4); ++i)
      top = std::max(top, env[i]);
   F.overshoot = std::max(0.0, 20.0 * std::log10(top / (level + 1e-20) + 1e-20));
   const double offT = ns.off - ns.on;
   F.rel20 = 0.6;
   for (size_t i = at(offT); i < env.size(); ++i)
      if (env[i] < 0.1 * level) {
         F.rel20 = (i * hop + win / 2) / kRate - offT;
         break;
      }

   // Tremolo: the envelope in dB over the held part.
   {
      std::vector<double> e;
      for (size_t i = at(0.4); i < at(kHold - win / kRate); i += 5) // every 10 ms
         e.push_back(20.0 * std::log10(env[i] + 1e-12));
      modulation(e, kRate / (hop * 5), F.am, F.amRate);
   }

   // The pitch track: every 10 ms, within 8% of the held pitch, for vibrato and how soon it settles.
   if (F.pitched) {
      const double L0 = kRate / F.f0;
      const int lo = std::max(2, static_cast<int>(L0 / 1.08)), hi = static_cast<int>(L0 * 1.08) + 1;
      const int flen = static_cast<int>(std::max(960.0, 2.0 * L0));
      std::vector<double> cents;
      std::vector<bool> ok;
      for (double t = 0.02; t + (flen + hi + 2) / kRate < kHold; t += 0.01) {
         bool edge = false;
         const double lag = frameLag(mid, on + static_cast<size_t>(t * kRate), flen, lo, hi, edge);
         cents.push_back(1200.0 * std::log2(L0 / lag));
         ok.push_back(!edge);
      }
      F.settle = 0.02;
      for (size_t i = 0; i < cents.size(); ++i)
         if (!ok[i] || std::fabs(cents[i]) > 20.0)
            F.settle = 0.02 + 0.01 * (i + 1);
      // Only the late stretch counts for settling: vibrato past 20 cents isn't a scoop.
      F.settle = std::min(F.settle, 0.4);
      std::vector<double> track;
      for (size_t i = 0; i < cents.size(); ++i)
         if (0.02 + 0.01 * i >= 0.4 && ok[i])
            track.push_back(cents[i]);
      modulation(track, 100.0, F.fm, F.fmRate);
   }

   // Clicks: the sharpest second difference at the attack and the release, over the held sound's
   // RMS of it, on either channel.
   for (const std::vector<float> *ch : {&rd.l, &rd.r}) {
      const std::vector<float> &x = *ch;
      double ref = 0;
      for (size_t i = s0; i < s1; ++i) {
         const double d2 = x[i] - 2.0 * x[i - 1] + x[i - 2];
         ref += d2 * d2;
      }
      ref = std::sqrt(ref / len);
      double worst = 0;
      const auto scan = [&](size_t a0, size_t a1) {
         for (size_t i = std::max<size_t>(a0, 2); i < std::min(a1, total); ++i)
            worst = std::max(worst, std::fabs(x[i] - 2.0 * x[i - 1] + x[i - 2]));
      };
      scan(on, on + static_cast<size_t>(0.06 * kRate));
      scan(off, off + static_cast<size_t>(0.15 * kRate));
      F.click = std::max(F.click, 20.0 * std::log10(worst / (ref + 1e-20) + 1e-20));
   }
   return F;
}

// ---- Comparing ------------------------------------------------------------------------------

// A param that moves the sound by less than this many JNDs over its whole travel is WEAK: you can
// hear it side by side, but not as something to play with.
constexpr double kClear = 3.0;

constexpr int kMeasures = 14;
const char *const kMeasureNames[kMeasures] = {"level", "spectrum", "harmonics", "centroid", "pitch", "onset", "overshoot",
                                              "release", "settle", "noise", "tremolo", "vibrato", "mod rate", "silence"};
const char *const kMeasureUnits[kMeasures] = {"dB", "dB", "dB", "%", "ct", "%", "dB", "%", "%", "dB", "dB", "ct", "%", ""};

struct Diff {
   double raw[kMeasures] = {};   // the difference in its unit (the largest over the notes)
   double score[kMeasures] = {}; // the same over its JND
   int seg[kMeasures] = {};
   double total = 0;
   int main = 0, mainSeg = 0;
};

double relTime(double a, double b, double frac, double floor, double &raw) {
   raw = 100.0 * (b - a) / std::max(1e-3, std::min(a, b));
   return std::fabs(b - a) / std::max(floor, frac * std::min(a, b));
}

Diff compare(const Feat *a, const Feat *b) {
   Diff D;
   const auto put = [&](int m, int seg, double raw, double score) {
      if (score > D.score[m]) {
         D.score[m] = score;
         D.raw[m] = raw;
         D.seg[m] = seg;
      }
   };
   for (int s = 0; s < kSegs; ++s) {
      const Feat &x = a[s], &y = b[s];
      if (x.silent || y.silent || !x.finite || !y.finite) {
         if (x.silent != y.silent || x.finite != y.finite)
            put(13, s, 1, 99.0);
         continue;
      }
      {
         double best = 0, raw = 0;
         for (auto [p, q] : {std::pair{x.rmsDb, y.rmsDb}, {x.aDb, y.aDb}, {x.lDb, y.lDb}, {x.rDb, y.rDb}})
            if (std::fabs(q - p) > best) {
               best = std::fabs(q - p);
               raw = q - p;
            }
         put(0, s, raw, best / jnd::level);
      }
      double sb = 0;
      int nb = 0;
      for (int k = 0; k < kBands; ++k)
         if (x.bands[k] > -60 || y.bands[k] > -60) {
            sb += (x.bands[k] - y.bands[k]) * (x.bands[k] - y.bands[k]);
            ++nb;
         }
      const double lsd = nb ? std::sqrt(sb / nb) : 0.0;
      put(1, s, lsd, lsd / jnd::bands);
      put(3, s, 100.0 * (y.centroid / x.centroid - 1.0), 100.0 * std::fabs(y.centroid / x.centroid - 1.0) / jnd::centroid);
      double r = 0;
      const double os = relTime(x.t90, y.t90, jnd::onset, jnd::onsetMin, r);
      put(5, s, r, os);
      put(6, s, y.overshoot - x.overshoot, std::fabs(y.overshoot - x.overshoot) / jnd::overshoot);
      const double rs = relTime(x.rel20, y.rel20, jnd::release, jnd::releaseMin, r);
      put(7, s, r, rs);
      put(10, s, y.am - x.am, std::fabs(y.am - x.am) / jnd::am);
      if (x.pitched && y.pitched) {
         double sh = 0;
         for (int k = 0; k < 10; ++k)
            sh += (x.harm[k] - y.harm[k]) * (x.harm[k] - y.harm[k]);
         sh = std::sqrt(sh / 10);
         put(2, s, sh, sh / jnd::harm);
         const double c = 1200.0 * std::log2(y.f0 / x.f0);
         put(4, s, c, std::fabs(c) / jnd::cents);
         const double ss = relTime(x.settle, y.settle, jnd::settle, jnd::settleMin, r);
         put(8, s, r, ss);
         put(9, s, y.nhr - x.nhr, std::fabs(y.nhr - x.nhr) / jnd::nhr);
         put(11, s, y.fm - x.fm, std::fabs(y.fm - x.fm) / jnd::fm);
      } else if (x.pitched != y.pitched) {
         put(2, s, 99, 99.0 / jnd::harm);
      }
      // The rate of whichever modulation both renders clearly have.
      const auto rate = [&](double dx, double dy, double rx, double ry, double j) {
         if (dx > 2 * j && dy > 2 * j && rx > 0 && ry > 0)
            put(12, s, 100.0 * (ry / rx - 1.0), std::fabs(ry / rx - 1.0) / jnd::rate);
      };
      rate(x.am, y.am, x.amRate, y.amRate, jnd::am);
      if (x.pitched && y.pitched)
         rate(x.fm, y.fm, x.fmRate, y.fmRate, jnd::fm);
   }
   for (int m = 0; m < kMeasures; ++m)
      if (D.score[m] > D.total) {
         D.total = D.score[m];
         D.main = m;
         D.mainSeg = D.seg[m];
      }
   return D;
}

std::string describe(const Diff &d) {
   if (d.total <= 0)
      return "nothing";
   char t[128];
   if (d.main == 13)
      std::snprintf(t, sizeof t, "silence (%s note)", kSegNames[d.mainSeg]);
   else
      std::snprintf(t, sizeof t, "%s %+.1f %s (%s)", kMeasureNames[d.main], d.raw[d.main], kMeasureUnits[d.main],
                    kSegNames[d.mainSeg]);
   return t;
}

// ---- The run --------------------------------------------------------------------------------

struct Job {
   int model;
   std::vector<std::pair<clap_id, double>> set;
   bool legato = false;
   Feat feat[kSegs];
};

std::string key(int model, const std::vector<std::pair<clap_id, double>> &set, bool legato) {
   auto s = set;
   std::sort(s.begin(), s.end());
   std::string k = std::to_string(model) + (legato ? "L" : "P");
   for (auto &p : s) {
      if (p.second == kParams[p.first].defaultValue)
         continue; // the default is the default
      char b[64];
      std::snprintf(b, sizeof b, "|%u=%.6g", p.first, p.second);
      k += b;
   }
   return k;
}

struct Row {
   clap_id id;
   int model;
   std::vector<double> values;
   std::vector<int> jobs;
   int lo, mid, hi, base = -1; // base: the default render, in the param's context
   Diff lm, mh, lh;
   double score = 0, choiceMin = 1e9;
   std::string sameAs;
   std::vector<std::string> flags;
   std::string cls;
};

void writeWav(const std::string &path, const Render &r) {
   FILE *f = std::fopen(path.c_str(), "wb");
   if (!f)
      return;
   const uint32_t n = static_cast<uint32_t>(r.l.size()), bytes = n * 4, rate = static_cast<uint32_t>(kRate);
   const auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
   const auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f);
   u32(36 + bytes);
   std::fwrite("WAVEfmt ", 1, 8, f);
   u32(16);
   u16(1);
   u16(2);
   u32(rate);
   u32(rate * 4);
   u16(4);
   u16(16);
   std::fwrite("data", 1, 4, f);
   u32(bytes);
   for (uint32_t i = 0; i < n; ++i)
      for (float v : {r.l[i], r.r[i]}) {
         const int16_t s = static_cast<int16_t>(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, -1.0f, 1.0f) * 32767.0f));
         std::fwrite(&s, 2, 1, f);
      }
   std::fclose(f);
}

std::string lower(std::string s) {
   for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   return s;
}
bool matchesAny(const std::string &name, const std::vector<std::string> &filters) {
   if (filters.empty())
      return true;
   for (const auto &f : filters)
      if (lower(name).find(lower(f)) != std::string::npos)
         return true;
   return false;
}
std::string slug(std::string s) {
   for (char &c : s)
      if (!std::isalnum(static_cast<unsigned char>(c)))
         c = '_';
   return s;
}
std::string valueText(clap_id id, double v) {
   char t[64];
   formatParam(id, v, t, sizeof t);
   return t;
}

} // namespace

int main(int argc, char **argv) {
   std::string outDir = ".", wavDir;
   unsigned threads = std::max(1u, std::thread::hardware_concurrency());
   std::vector<std::string> modelFilter, paramFilter;
   bool dump = false;
   for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
      if (a == "--out")
         outDir = next();
      else if (a == "--wav")
         wavDir = next();
      else if (a == "--threads")
         threads = static_cast<unsigned>(std::max(1, std::atoi(next().c_str())));
      else if (a == "--model")
         modelFilter.push_back(next());
      else if (a == "--param")
         paramFilter.push_back(next());
      else if (a == "--features")
         dump = true;
      else {
         std::printf("usage: %s [--out DIR] [--wav DIR] [--threads N] [--model NAME] [--param NAME] [--features]\n", argv[0]);
         return a == "--help" || a == "-h" ? 0 : 2;
      }
   }

   std::vector<int> models;
   for (int m = 0; m < kModelCount; ++m)
      if (matchesAny(modelInfo(m).name, modelFilter))
         models.push_back(m);
   std::vector<clap_id> params;
   std::vector<std::pair<clap_id, const char *>> skipped;
   for (clap_id id = 0; id < kParamCount; ++id) {
      if (const char *why = skipReason(id)) {
         if (!(id >= kLfo1Rate && id < kRouteEnd) && !isLookParam(id))
            skipped.push_back({id, why});
         continue;
      }
      if (matchesAny(kParams[id].name, paramFilter))
         params.push_back(id);
   }

   // Every render, once.
   std::vector<Job> jobs;
   std::map<std::string, int> index;
   const auto job = [&](int model, std::vector<std::pair<clap_id, double>> set, bool legato) {
      const std::string k = key(model, set, legato);
      auto it = index.find(k);
      if (it != index.end())
         return it->second;
      Job j;
      j.model = model;
      j.set = std::move(set);
      j.legato = legato;
      jobs.push_back(std::move(j));
      return index[k] = static_cast<int>(jobs.size()) - 1;
   };
   std::vector<int> base(kModelCount, -1);
   std::vector<Row> rows;
   for (int m : models) {
      base[m] = job(m, {}, false);
      for (clap_id id : params) {
         const Context c = contextFor(id);
         Row r;
         r.id = id;
         r.model = m;
         r.values = valuesFor(id);
         lowMidHigh(id, r.values, r.lo, r.mid, r.hi);
         r.base = job(m, c.set, c.legato);
         for (double v : r.values) {
            auto set = c.set;
            set.push_back({id, v});
            r.jobs.push_back(job(m, set, c.legato));
         }
         rows.push_back(std::move(r));
      }
   }

   std::printf("knob audit: %zu params x %zu models, %zu renders on %u threads\n", params.size(), models.size(),
               jobs.size(), threads);
   std::fflush(stdout);
   std::atomic<size_t> next{0}, done{0};
   std::mutex print;
   std::vector<std::thread> pool;
   for (unsigned t = 0; t < threads; ++t)
      pool.emplace_back([&] {
         for (size_t i; (i = next++) < jobs.size();) {
            Job &j = jobs[i];
            const Render r = render(j.model, j.set, j.legato);
            const Phrase ph = phraseFor(j.model, j.legato);
            for (int s = 0; s < kSegs; ++s)
               j.feat[s] = analyse(r, ph.notes[s]);
            const size_t d = ++done;
            if (d % 50 == 0 || d == jobs.size()) {
               std::lock_guard<std::mutex> lock(print);
               std::printf("  %zu/%zu\n", d, jobs.size());
               std::fflush(stdout);
            }
         }
      });
   for (auto &t : pool)
      t.join();

   if (dump) {
      // Each model's default render, note by note: a check that the measures read sensibly.
      std::printf("\n%-9s %-5s %7s %7s %7s %7s %6s %6s %6s %6s %6s %6s %5s %5s %5s %5s %6s  H1..H10 dB\n", "model", "note",
                  "f0", "rms dB", "A dB", "cntrd", "NHR", "t90ms", "ovsht", "rel ms", "set ms", "AM dB", "AMHz", "FMct",
                  "FMHz", "click", "pitch");
      for (int m : models) {
         const Job &j = jobs[base[m]];
         for (int s = 0; s < kSegs; ++s) {
            const Feat &f = j.feat[s];
            std::printf("%-9s %-5s %7.1f %7.1f %7.1f %7.0f %6.1f %6.0f %6.1f %6.0f %6.0f %6.2f %5.1f %5.1f %5.1f %5.1f %6s ",
                        modelInfo(m).name, kSegNames[s], f.f0, f.rmsDb, f.aDb, f.centroid, f.nhr, 1000 * f.t90, f.overshoot,
                        1000 * f.rel20, 1000 * f.settle, f.am, f.amRate, f.fm, f.fmRate, f.click, f.pitched ? "yes" : "no");
            for (double h : f.harm)
               std::printf(" %5.1f", h);
            std::printf("\n");
         }
      }
   }

   // Score every row.
   for (Row &r : rows) {
      const Feat *f[16];
      for (size_t i = 0; i < r.jobs.size(); ++i)
         f[i] = jobs[r.jobs[i]].feat;
      r.lm = compare(f[r.lo], f[r.mid]);
      r.mh = compare(f[r.mid], f[r.hi]);
      r.lh = compare(f[r.lo], f[r.hi]);
      r.score = std::max({r.lm.total, r.mh.total, r.lh.total});
      if (isChoice(r.id)) {
         for (size_t i = 0; i < r.jobs.size(); ++i)
            for (size_t k = i + 1; k < r.jobs.size(); ++k) {
               const Diff d = compare(f[i], f[k]);
               r.score = std::max(r.score, d.total);
               if (d.total < r.choiceMin) {
                  r.choiceMin = d.total;
                  r.sameAs = valueText(r.id, r.values[i]) + " ~ " + valueText(r.id, r.values[k]);
               }
            }
         if (r.choiceMin < 1.0)
            r.flags.push_back("SAME(" + r.sameAs + ")");
      }
      r.cls = r.score >= kClear ? "OK" : r.score >= 1.0 ? "WEAK" : "INAUDIBLE";
      const double big = std::max(r.lm.total, r.mh.total), small = std::min(r.lm.total, r.mh.total);
      if (!isChoice(r.id) && big >= 2.0 && r.lh.total < 0.6 * big)
         r.flags.push_back("NON-MONOTONIC");
      if (!isChoice(r.id) && big >= 2.0 && small < 0.5)
         r.flags.push_back(r.lm.total < r.mh.total ? "ONE-SIDED(low half dead)" : "ONE-SIDED(high half dead)");
      // Broken renders, against the model's default (in the param's context): which value, which notes. A Level or Master
      // at its ends is meant to be silent or to clip.
      const Feat *b = jobs[r.base].feat;
      const bool levelParam = r.id == kLevel || r.id == kMaster;
      std::map<std::string, std::vector<std::string>> found; // flag -> "value: notes"
      for (size_t i = 0; i < r.jobs.size(); ++i) {
         std::map<std::string, std::string> notes;
         double worstClick = -1e9;
         for (int s = 0; s < kSegs; ++s) {
            const Feat &x = f[i][s];
            const auto add = [&](const char *flag) { notes[flag] += std::string(notes[flag].empty() ? "" : ",") + kSegNames[s]; };
            if (!x.finite)
               add("BROKEN");
            if (!levelParam && !b[s].silent && (x.silent || x.rmsDb < b[s].rmsDb - 30.0))
               add("SILENT");
            if (!levelParam && x.peak >= 0.999)
               add("CLIP");
            if (!levelParam && !x.silent && x.rmsDb > b[s].rmsDb + 12.0)
               add("LOUD");
            if (!x.silent && x.click > std::max(b[s].click + 6.0, 20.0)) {
               add("CLICK");
               worstClick = std::max(worstClick, x.click - b[s].click);
            }
         }
         for (auto &[flag, segs] : notes) {
            std::string t = valueText(r.id, r.values[i]) + ": " + segs;
            if (flag == "CLICK") {
               char d[32];
               std::snprintf(d, sizeof d, " +%.0f dB", worstClick);
               t += d;
            }
            found[flag].push_back(t);
         }
      }
      for (const char *flag : {"BROKEN", "SILENT", "CLIP", "LOUD", "CLICK"}) {
         auto it = found.find(flag);
         if (it == found.end())
            continue;
         std::string t = flag + std::string("(");
         for (size_t k = 0; k < it->second.size(); ++k)
            t += (k ? "; " : "") + it->second[k];
         r.flags.push_back(t + ")");
      }
   }

   // Per param, over the models.
   struct Verdict {
      std::string cls, note;
   };
   std::map<clap_id, Verdict> verdicts;
   for (clap_id id : params) {
      int audible = 0, weak = 0, n = 0;
      std::string deaf;
      const std::vector<int> meant = intendedModels(id);
      bool meantDeaf = false, onlyMeant = true;
      for (const Row &r : rows) {
         if (r.id != id)
            continue;
         ++n;
         const bool isMeant = meant.empty() || std::find(meant.begin(), meant.end(), r.model) != meant.end();
         if (r.score >= 1.0) {
            ++audible;
            if (r.score < kClear)
               ++weak;
            if (!isMeant)
               onlyMeant = false;
         } else {
            deaf += std::string(deaf.empty() ? "" : ", ") + modelInfo(r.model).name;
            if (isMeant)
               meantDeaf = true;
         }
      }
      Verdict v;
      if (audible == 0)
         v.cls = "INAUDIBLE";
      else if (audible < n) {
         v.cls = "MODEL-SPECIFIC";
         v.note = "inaudible on " + deaf;
         if (!meant.empty() && !meantDeaf && onlyMeant)
            v.note += " (by design)";
      } else if (2 * weak > n)
         v.cls = "WEAK";
      else
         v.cls = "OK";
      verdicts[id] = v;
   }

   // The table, the CSV and the Markdown.
   const std::string csvPath = outDir + "/knob_audit.csv", mdPath = outDir + "/knob_audit.md";
   FILE *csv = std::fopen(csvPath.c_str(), "w");
   FILE *md = std::fopen(mdPath.c_str(), "w");
   if (!csv || !md) {
      std::printf("can't write to %s\n", outDir.c_str());
      return 1;
   }
   std::fprintf(csv, "param,group,model,context,min,centre,max,score_min_centre,score_centre_max,score_min_max,score,"
                     "class,param_class,main_change");
   for (int m = 0; m < kMeasures - 1; ++m)
      std::fprintf(csv, ",d_%s_%s", slug(kMeasureNames[m]).c_str(), kMeasureUnits[m]);
   std::fprintf(csv, ",closest_choices,flags\n");
   for (const Row &r : rows) {
      const auto &p = kParams[r.id];
      std::string fl;
      for (const auto &f : r.flags)
         fl += (fl.empty() ? "" : " ") + f;
      std::fprintf(csv, "\"%s\",%s,%s,\"%s\",\"%s\",\"%s\",\"%s\",%.2f,%.2f,%.2f,%.2f,%s,%s,\"%s\"", p.name, p.module,
                   modelInfo(r.model).name, contextFor(r.id).note, valueText(r.id, r.values[r.lo]).c_str(),
                   valueText(r.id, r.values[r.mid]).c_str(), valueText(r.id, r.values[r.hi]).c_str(), r.lm.total,
                   r.mh.total, r.lh.total, r.score, r.cls.c_str(), verdicts[r.id].cls.c_str(), describe(r.lh).c_str());
      for (int m = 0; m < kMeasures - 1; ++m)
         std::fprintf(csv, ",%.2f", r.lh.raw[m]);
      std::fprintf(csv, ",\"%s\",\"%s\"\n", isChoice(r.id) ? r.sameAs.c_str() : "", fl.c_str());
   }
   std::fclose(csv);

   std::fprintf(md, "# JBreath knob audit\n\n");
   std::fprintf(md, "Each cell: the audibility score of min vs centre / centre vs max (1.0 = one just-noticeable "
                    "difference), and the class. See tools/knob_audit.cpp for the measures and JNDs.\n\n");
   std::fprintf(md, "Notes per model (low, high, soft/loud):");
   for (int m : models) {
      int k[3];
      notesFor(m, k);
      std::fprintf(md, " %s %d/%d/%d;", modelInfo(m).name, k[0], k[1], k[2]);
   }
   std::fprintf(md, "\n\n| Param | Range | Context |");
   for (int m : models)
      std::fprintf(md, " %s |", modelInfo(m).name);
   std::fprintf(md, " Verdict |\n|---|---|---|");
   for (size_t i = 0; i < models.size(); ++i)
      std::fprintf(md, "---|");
   std::fprintf(md, "---|\n");

   std::printf("\n%-22s %-15s", "param", "context");
   for (int m : models)
      std::printf(" %-15.15s", modelInfo(m).name);
   std::printf(" verdict\n");
   for (clap_id id : params) {
      const auto &p = kParams[id];
      const Context c = contextFor(id);
      std::fprintf(md, "| %s | %s .. %s | %s |", p.name, valueText(id, p.min).c_str(), valueText(id, p.max).c_str(), c.note);
      std::printf("%-22.22s %-15.15s", p.name, c.note);
      for (const Row &r : rows) {
         if (r.id != id)
            continue;
         std::string fl;
         for (const auto &f : r.flags)
            fl += " " + f;
         std::fprintf(md, " %.1f / %.1f %s%s |", r.lm.total, r.mh.total, r.cls.c_str(), fl.c_str());
         char cell[64];
         std::snprintf(cell, sizeof cell, "%.1f/%.1f %s", r.lm.total, r.mh.total,
                       r.cls == "INAUDIBLE" ? "--" : r.cls == "WEAK" ? "w" : "");
         std::printf(" %-15s", cell);
      }
      const Verdict &v = verdicts[id];
      std::fprintf(md, " **%s** %s |\n", v.cls.c_str(), v.note.c_str());
      std::printf(" %s %s\n", v.cls.c_str(), v.note.c_str());
   }

   std::fprintf(md, "\n## What changes most, min vs max\n\n| Param | Model | Score | Main change | Flags |\n|---|---|---|---|---|\n");
   for (const Row &r : rows) {
      std::string fl;
      for (const auto &f : r.flags)
         fl += (fl.empty() ? "" : ", ") + f;
      std::fprintf(md, "| %s | %s | %.1f | %s | %s |\n", kParams[r.id].name, modelInfo(r.model).name, r.score,
                   describe(r.lh).c_str(), fl.c_str());
   }
   std::fprintf(md, "\n## Not audited\n\n");
   for (auto &[id, why] : skipped)
      std::fprintf(md, "- %s: %s\n", kParams[id].name, why);
   std::fprintf(md, "- LFO, Swell, bus and route params: modulation plumbing\n- The AIR view's look params\n");
   std::fclose(md);

   std::printf("\nflags:\n");
   for (const Row &r : rows)
      if (!r.flags.empty()) {
         std::printf("  %-20s %-9s", kParams[r.id].name, modelInfo(r.model).name);
         for (const auto &f : r.flags)
            std::printf(" %s", f.c_str());
         std::printf("\n");
      }
   std::printf("\nwrote %s and %s\n", csvPath.c_str(), mdPath.c_str());

   if (!wavDir.empty()) {
      std::vector<std::pair<int, std::string>> wavs;
      for (const Row &r : rows) {
         if (verdicts[r.id].cls == "OK" && r.flags.empty())
            continue;
         for (int which : {r.lo, r.mid, r.hi})
            wavs.push_back({r.jobs[which], wavDir + "/" + slug(modelInfo(r.model).name) + "_" + slug(kParams[r.id].name) +
                                              "_" + (which == r.lo ? "min" : which == r.hi ? "max" : "centre") + ".wav"});
      }
      std::atomic<size_t> w{0};
      std::vector<std::thread> writers;
      for (unsigned t = 0; t < threads; ++t)
         writers.emplace_back([&] {
            for (size_t i; (i = w++) < wavs.size();) {
               const Job &j = jobs[wavs[i].first];
               writeWav(wavs[i].second, render(j.model, j.set, j.legato));
            }
         });
      for (auto &t : writers)
         t.join();
      std::printf("wrote %zu WAVs to %s\n", wavs.size(), wavDir.c_str());
   }
   return 0;
}
