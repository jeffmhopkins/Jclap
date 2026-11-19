#include "gen/orb/render.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace jkit::orb {
namespace {

using Params = std::map<std::string, double>;

double get(const Params &p, const char *key, double fallback) {
   const auto it = p.find(key);
   return it == p.end() ? fallback : it->second;
}

constexpr int sr = kSr;
constexpr double kAttackHi = 12000.0;

size_t lengthOf(const Event &ev) { return static_cast<size_t>(std::max<int64_t>(1, toInt(ev.dur * sr))); }

Vec arangeOverN(size_t n) { // np.arange(n) / n
   Vec v(n);
   for (size_t i = 0; i < n; ++i)
      v[i] = static_cast<double>(i) / static_cast<double>(n);
   return v;
}

Vec expDecayOverN(size_t n, double rate) { // np.exp(-rate * np.arange(n) / n)
   Vec v(n);
   for (size_t i = 0; i < n; ++i)
      v[i] = std::exp(-rate * static_cast<double>(i) / static_cast<double>(n));
   return v;
}

Vec maxOf(const Vec &a, double floor) {
   return map(a, [&](double v) { return std::max(v, floor); });
}

Vec concat(const Vec &a, const Vec &b) {
   Vec r = a;
   r.insert(r.end(), b.begin(), b.end());
   return r;
}

// ============================================================================
// events.py
// ============================================================================

Vec whoosh(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const double apexFrac = clip((ev.apex - ev.t) / ev.dur, 0.05, 0.95);
   const Vec env = envApex(n, sr, apexFrac * ev.dur, 1.5, 3.0);
   const Vec noise = rng.normal(n);
   const Vec centers =
      interp(linspace(0.0, 1.0, 64), {0.0, apexFrac, 1.0}, {get(p, "f_start", 0), get(p, "f_peak", 0), get(p, "f_end", 0)});
   const Vec widths = maxOf(centers * 0.55, 120.0);
   const Vec air = bandpassTimevarying(noise, centers, widths, sr) * env;
   const double bodyF = get(p, "body_f", 0);
   const Vec body = sweepSine(bodyF, bodyF * 0.55, ev.dur, sr) * env * 0.3;
   return air + body;
}

Vec impact(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const double t60 = get(p, "t60", 0), f0 = get(p, "f0", 0);
   const Vec body = modal(f0, ev.dur, sr, {1.0, 2.32, 3.71, 5.13}, {t60, t60 * 0.6, t60 * 0.35, t60 * 0.2},
                          {1.0, 0.7, 0.4, 0.25});
   const Vec click = shapedBurst(rng, n, toInt(0.005 * sr), 10.0, sr, 1000.0, 9000.0);
   const Vec sub = sweepSine(f0 * 0.7, f0 * 0.35, ev.dur, sr) * expDecayOverN(n, 5.0);
   Vec mix(n);
   for (size_t i = 0; i < n; ++i)
      mix[i] = body[i] + 0.6 * click[i] + 0.7 * sub[i];
   return bandlimit(saturate(mix, 1.8), sr, -1, kAttackHi, 8);
}

Vec uiPop(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const double t60 = get(p, "t60", 0);
   const Vec body = modal(get(p, "f0", 0), ev.dur, sr, {1.0, 2.7}, {t60, t60 * 0.4}, {1.0, 0.3});
   const Vec click = shapedBurst(rng, n, toInt(0.002 * sr), 8.0, sr, 1500.0, 10000.0);
   return body + 0.25 * click;
}

Vec riser(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const double apexS = clip(ev.apex - ev.t, 0.05 * ev.dur, 0.99 * ev.dur);
   const Vec env = envApex(n, sr, apexS, 2.0, 8.0);
   const Vec noise = rng.normal(n);
   const Vec centers = interp(linspace(0.0, 1.0, 64), {0.0, 1.0}, {get(p, "f_start", 0), get(p, "f_peak", 0)});
   const Vec widths = maxOf(centers * 0.45, 150.0);
   const Vec air = bandpassTimevarying(noise, centers, widths, sr);
   const double bodyF = get(p, "body_f", 0);
   const Vec tone = sweepSine(bodyF, bodyF * 8.0, ev.dur, sr, 2.0) * 0.15;
   return (air + tone) * env;
}

Vec swell(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const double apexS = clip(ev.apex - ev.t, 0.05 * ev.dur, 0.99 * ev.dur);
   const Vec env = envApex(n, sr, apexS, 3.0, 6.0);
   const Vec noise = rng.normal(n);
   const double fPeak = get(p, "f_peak", 0);
   const Vec centers(64, fPeak);
   const Vec widths(64, std::max(fPeak * 0.8, 200.0));
   const Vec air = bandpassTimevarying(noise, centers, widths, sr);
   const Vec shimmer = modal(fPeak * 0.5, ev.dur, sr, {1.0, 2.01, 3.0}, {0.6, 0.4, 0.25}, {1.0, 0.6, 0.35, 0.2});
   return (air * 0.8 + shimmer * 0.15) * env;
}

Vec click(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   Vec out = shapedBurst(rng, n, toInt(0.02 * sr), 14.0, sr, 2000.0, kAttackHi);
   const double t60 = get(p, "t60", 0);
   out += modal(get(p, "f0", 0), ev.dur, sr, {1.0, 1.9}, {t60, t60 * 0.5}, {1.0, 0.6, 0.35, 0.2}) * 0.4;
   return out;
}

Vec bell(const Event &ev, Generator &, const Params &p) {
   const double t60 = get(p, "t60", 0);
   return modal(get(p, "f0", 0), ev.dur, sr, {1.0, 2.76, 5.40, 8.93}, {t60, t60 * 0.6, t60 * 0.4, t60 * 0.25},
                {1.0, 0.6, 0.35, 0.2});
}

Vec subDrop(const Event &ev, Generator &, const Params &p) {
   const size_t n = lengthOf(ev);
   return sweepSine(get(p, "f0", 0), get(p, "f_end", 0), ev.dur, sr, 0.6) * expDecayOverN(n, 3.0);
}

Vec glitch(const Event &ev, Generator &rng, const Params &) {
   const size_t n = lengthOf(ev);
   const int64_t steps = rng.integers(3, 9);
   const size_t hold = std::max<size_t>(1, n / static_cast<size_t>(steps));
   const Vec vals = rng.normal(static_cast<size_t>(steps + 1));
   const Vec decay = expDecayOverN(n, 3.0);
   Vec sig(n);
   for (size_t i = 0; i < n; ++i) {
      const size_t idx = std::min<size_t>(i / hold, static_cast<size_t>(steps));
      sig[i] = roundEven(vals[idx] * decay[i] * 8.0) / 8.0;
   }
   return bandlimit(saturate(sig, 1.2), sr, -1, kAttackHi, 8);
}

Vec noiseSweep(const Event &ev, Generator &rng, const Params &p) {
   const size_t n = lengthOf(ev);
   const Vec env = envApex(n, sr, 0.5 * ev.dur, 1.2, 2.0);
   const Vec noise = rng.normal(n);
   const Vec centers = interp(linspace(0.0, 1.0, 64), {0.0, 0.5, 1.0},
                              {get(p, "f_start", 0), get(p, "f_peak", 0), get(p, "f_end", 0)});
   const Vec widths = maxOf(centers * 0.6, 150.0);
   return bandpassTimevarying(noise, centers, widths, sr) * env;
}

bool continuousRole(const std::string &role) {
   return role == "whoosh" || role == "riser" || role == "swell" || role == "noise_sweep";
}

Vec eventEdges(const Vec &mono, const std::string &role) {
   if (!continuousRole(role))
      return fade(mono, sr, 0.002, 0.01);
   Vec x = mono;
   const size_t n = x.size();
   const size_t a = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(2, toInt(0.005 * sr))));
   const size_t b = std::min<size_t>(
      n, static_cast<size_t>(std::max<int64_t>(2, toInt(clip(0.06 * static_cast<double>(n) / sr, 0.02, 0.08) * sr))));
   const Vec la = linspace(0.0, kPi, a), lb = linspace(0.0, kPi, b);
   for (size_t i = 0; i < a; ++i)
      x[i] *= 0.5 - 0.5 * std::cos(la[i]);
   for (size_t i = 0; i < b; ++i)
      x[n - b + i] *= 0.5 + 0.5 * std::cos(lb[i]);
   return x;
}

Vec wetOf(const Vec &mono, const Params &p, double wet, uint64_t seed, bool tail) {
   Vec out = reverb(mono, sr, get(p, "reverb_decay", 0.5), wet, seed, tail);
   if (!tail) {
      const size_t b = std::min<size_t>(out.size(), static_cast<size_t>(std::max<int64_t>(1, toInt(0.01 * sr))));
      const Vec l = linspace(1.0, 0.0, b);
      for (size_t i = 0; i < b; ++i)
         out[out.size() - b + i] *= l[i];
   }
   return out;
}

// ============================================================================
// foley.py
// ============================================================================

using Foley = void (*)(const Event &, Generator &, Generator &, Params &, int, Vec &, Vec &);

void doorSlam(const Event &ev, Generator &rng, Generator &crng, Params &p, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double weight = (std::vector<double>{1.0, 0.8, 0.62})[static_cast<size_t>(seed % 3)];
   const int room = (seed / 3) % 3;
   const bool swing = (seed / 9) % 2 == 1;
   const double t0 = 0.002 + (swing ? 0.11 : 0.0);
   const int64_t at = toInt(t0 * sr);
   const Vec t = times(n, sr);

   const double fw = crng.uniform(46.0, 58.0) / std::pow(weight, 0.8);
   const int64_t L = std::min<int64_t>(static_cast<int64_t>(n) - at, toInt(1.5 / fw * sr));
   Vec whump(n, 0.0);
   if (L > 2) {
      for (int64_t i = 0; i < L; ++i) {
         const double tw = static_cast<double>(i) / sr;
         const double s = std::sin(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(L));
         whump[static_cast<size_t>(at + i)] = std::sin(kTau * fw * tw) * (s * s);
      }
   }
   const Vec puff = bandlimit(burst(rng, n, at, toInt(0.15 * sr), 0.03, sr, 0.004), sr, 30.0, 160.0, 2);
   whump = norm(whump) + 0.45 * norm(puff);

   const double fLo = 74.0 / weight * crng.uniform(0.94, 1.06);
   const Vec loU = crng.uniform(0.97, 1.03, 4);
   const Vec loF = (Vec{1.0, 1.29, 1.71, 2.18} * fLo) * loU;
   const Vec loT = Vec{0.25, 0.21, 0.17, 0.13} * (0.65 + 0.35 * weight);
   const Vec loG = {1.0, 0.8, 0.6, 0.4};
   const Vec push = pulse(n, at, toInt((0.006 + 0.004 * weight) * sr));
   const Vec bodyLo = fftconv(push, modesIr(loF, loT, loG, sr, n), n);
   Vec midF = crng.uniform(250.0, 700.0, 6);
   std::sort(midF.begin(), midF.end());
   midF = midF / std::pow(weight, 0.3);
   const Vec midT = crng.uniform(0.05, 0.11, 6);
   const Vec midG = crng.uniform(0.5, 1.0, 6) * linspace(1.0, 0.5, 6);
   const Vec contact = pulse(n, at, toInt(0.0018 * sr)) + 0.3 * burst(rng, n, at, toInt(0.006 * sr), 0.0015, sr);
   const Vec bodyMid = fftconv(contact, modesIr(midF, midT, midG, sr, n), n);
   const Vec body = 0.6 * norm(bodyLo) + 1.4 * norm(bodyMid);

   const Vec frame = bandlimit(burst(rng, n, at, toInt(0.05 * sr), 0.009, sr, 0.0012), sr, 110.0, 2600.0, 4);

   const double lf = crng.uniform(2300.0, 3600.0);
   const Vec latchIr = modesIr({lf, lf * 1.47, lf * 1.93}, {0.03, 0.022, 0.015}, {1.0, 0.55, 0.35}, sr, n);
   const int64_t lAt = at + toInt(crng.uniform(0.004, 0.016) * sr);
   Vec latch = fftconv(burst(rng, n, lAt, toInt(0.002 * sr), 0.0005, sr), latchIr, n);
   if (seed % 2) {
      const int64_t count = crng.integers(1, 3);
      for (int64_t k = 0; k < count; ++k) {
         const int64_t rAt = lAt + toInt(crng.uniform(0.025, 0.075) * sr);
         const double g = crng.uniform(0.25, 0.45);
         latch += g * fftconv(burst(rng, n, rAt, toInt(0.0015 * sr), 0.0004, sr), latchIr, n);
      }
   }
   const double latchDb = crng.uniform(-18.0, -13.0);

   Vec dry = 0.4 * whump + body + 0.9 * norm(frame);
   dry = norm(dry) + dbToGain(latchDb) * norm(latch);

   if (swing) {
      Vec env(n);
      for (size_t i = 0; i < n; ++i) {
         const double u = clip(t[i] / t0, 0.0, 1.0);
         env[i] = (0.5 - 0.5 * std::cos(kPi * u)) * std::exp(-std::max(t[i] - t0, 0.0) / 0.008);
      }
      const Vec air = bandlimit(rng.normal(n), sr, 150.0, 1200.0, 2);
      dry = dry + 0.1 * norm(air) * env;
   }

   const double span = (std::vector<double>{0.018, 0.04, 0.045})[static_cast<size_t>(room)];
   const double first = (std::vector<double>{0.003, 0.004, 0.008})[static_cast<size_t>(room)];
   const auto early = [&]() {
      Vec er(n, 0.0);
      for (int k = 0; k < 12; ++k) {
         const int64_t d = toInt(crng.uniform(first, first + span) * sr);
         if (d < static_cast<int64_t>(n)) {
            const double g = crng.uniform(0.08, 0.22) * std::exp(-(static_cast<double>(d) / sr) / 0.035);
            for (size_t i = static_cast<size_t>(d); i < n; ++i)
               er[i] += g * dry[i - static_cast<size_t>(d)];
         }
      }
      return bandlimit(er, sr, 80.0, 2800.0, 2);
   };
   p["reverb"] = (std::vector<double>{0.3, 0.36, 0.42})[static_cast<size_t>(room)];
   p["reverb_decay"] = (std::vector<double>{0.45, 0.7, 0.9})[static_cast<size_t>(room)];
   outL = dry + early();
   outR = dry + early();
}

void knock(const Event &ev, Generator &rng, Generator &crng, Params &p, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const int raps = 2 + seed % 2;
   const double size = (std::vector<double>{1.0, 0.85, 1.2})[static_cast<size_t>((seed / 2) % 3)];
   const double f0 = get(p, "f0", 0) / size * crng.uniform(0.95, 1.05);
   const Vec ratios = Vec{1.0, 1.59, 2.14, 2.65, 3.61, 4.43} * crng.uniform(0.97, 1.03, 6);
   const Vec t60 = {0.10, 0.08, 0.06, 0.045, 0.035, 0.025};
   const Vec gains = {1.0, 0.85, 0.7, 0.5, 0.35, 0.25};
   const double hiGap = crng.uniform(0.15, 0.21);
   const double gap = clip((dur - 0.15) / std::max(1, raps - 1), 0.07, hiGap);
   Vec force(n, 0.0), clk(n, 0.0);
   double tm = 0.002;
   for (int i = 0; i < raps; ++i) {
      const int64_t at = toInt(tm * sr);
      if (at >= static_cast<int64_t>(n))
         break;
      const double a = crng.uniform(0.8, 1.0) * (i == raps - 1 ? 1.08 : 1.0);
      const int64_t width = toInt(crng.uniform(0.0010, 0.0016) * sr);
      force += a * pulse(n, at, width);
      clk += a * burst(rng, n, at, toInt(0.003 * sr), 0.0007, sr);
      tm += gap * crng.uniform(0.92, 1.05);
   }
   const Vec panel = ring(force, ratios * f0, t60, gains, sr);
   const double doorF = crng.uniform(55, 75) / size;
   const Vec door = ring(force, {doorF}, {0.12}, {1.0}, sr);
   clk = bandlimit(clk, sr, 1200.0, 6000.0, 4);
   const Vec mono = norm(panel) + 0.25 * norm(door) + 0.22 * norm(clk);
   outL = mono;
   outR = mono;
}

Vec dripBubble(size_t n, double at, double f0, double amp, double damp, double rise) {
   Vec out(n, 0.0);
   const int64_t i = toInt(at * sr);
   if (i >= static_cast<int64_t>(n) || i < 0)
      return out;
   const double d = (0.043 * f0 + 0.0014 * std::pow(f0, 1.5)) * damp;
   const size_t k = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(n) - i, toInt(6.0 / d * sr)));
   const size_t a = std::min<size_t>(k, static_cast<size_t>(std::max<int64_t>(2, toInt(0.0006 * sr))));
   for (size_t j = 0; j < k; ++j) {
      const double t = static_cast<double>(j) / sr;
      double env = std::exp(-d * t);
      if (j < a)
         env *= 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(j) + 0.5) / static_cast<double>(a));
      out[static_cast<size_t>(i) + j] = amp * std::sin(kTau * f0 * (t + 0.5 * rise * d * t * t)) * env;
   }
   return out;
}

void waterDrop(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t nEv = lengthOf(ev);
   const size_t n = std::min<size_t>(nEv, static_cast<size_t>(toInt(0.6 * sr)));
   const double t0 = 0.004;
   Vec mono;
   if (seed % 2 == 0) {
      const double f0 = crng.uniform(1200.0, 2600.0);
      const Vec tick =
         bandlimit(burst(rng, n, toInt(t0 * sr), toInt(0.002 * sr), 0.0003, sr, 0.0002), sr, 1500.0, 11000.0, 2);
      const double bAt = t0 + crng.uniform(0.003, 0.008);
      const double bDamp = crng.uniform(0.9, 1.2);
      const double bRise = crng.uniform(0.2, 0.35);
      const Vec bub = dripBubble(n, bAt, f0, 1.0, bDamp, bRise);
      const double tj = t0 + crng.uniform(0.05, 0.11);
      const Vec jet =
         bandlimit(burst(rng, n, toInt(tj * sr), toInt(0.0015 * sr), 0.0002, sr, 0.0002), sr, 2500.0, 11000.0, 2);
      const Vec bub2 = dripBubble(n, tj + 0.002, f0 * crng.uniform(1.4, 2.0), 1.0, 1.1, 0.25);
      const Vec a = norm(bub) + 0.6 * norm(tick);
      const double g = crng.uniform(0.1, 0.3);
      mono = a + g * (norm(bub2) + 0.4 * norm(jet));
   } else {
      const double f0 = crng.uniform(550.0, 1100.0);
      const Vec tick =
         bandlimit(burst(rng, n, toInt(t0 * sr), toInt(0.004 * sr), 0.0006, sr, 0.0003), sr, 600.0, 10000.0, 2);
      const Vec crown = bandlimit(burst(rng, n, toInt(t0 * sr), toInt(0.05 * sr), 0.008, sr, 0.001), sr, 900.0, 8000.0, 2);
      const double bAt = t0 + crng.uniform(0.008, 0.016);
      const double bDamp = crng.uniform(0.55, 0.75);
      const double bRise = crng.uniform(0.3, 0.45);
      const Vec bub = dripBubble(n, bAt, f0, 1.0, bDamp, bRise);
      const double b2At = t0 + crng.uniform(0.02, 0.05);
      const Vec bub2 = dripBubble(n, b2At, f0 * crng.uniform(1.8, 2.6), 1.0, 1.0, 0.2);
      const double tj = t0 + crng.uniform(0.09, 0.16);
      const Vec bub3 = dripBubble(n, tj, f0 * crng.uniform(2.2, 3.0), 1.0, 1.1, 0.25);
      const Vec a = norm(bub) + 0.5 * norm(tick) + 0.35 * norm(crown) + 0.3 * norm(bub2);
      const double g = crng.uniform(0.15, 0.3);
      mono = a + g * norm(bub3);
   }
   mono = padZeros(mono, 0, nEv - n);
   outL = mono;
   outR = mono;
}

void explosion(const Event &ev, Generator &rng, Generator &crng, Params &p, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const double dist = (std::vector<double>{0.0, 0.4, 0.85})[static_cast<size_t>(seed % 3)];
   const double f0 = get(p, "f0", 0) * crng.uniform(0.85, 1.15);
   const double at = 0.002;

   const double slow = std::max(0.6, 0.3 * dur);
   Vec fSub(n);
   for (size_t i = 0; i < n; ++i)
      fSub[i] = f0 * (1.0 + 1.2 * std::exp(-t[i] / 0.06)) * (0.8 + 0.2 * std::exp(-t[i] / slow));
   const Vec cs = cumsum(fSub);
   const Vec att = attack(n, sr, at, 0.004);
   Vec sub(n);
   for (size_t i = 0; i < n; ++i)
      sub[i] = std::sin(kTau * cs[i] / sr) * att[i] * std::exp(-t[i] / 0.45);

   const Vec bright = bandlimit(burst(rng, n, toInt(at * sr), toInt(0.6 * sr), 0.07 + 0.03 * dist, sr), sr, 300.0,
                                9000.0 - 6000.0 * dist, 2);
   const Vec mid = bandlimit(burst(rng, n, toInt(at * sr), toInt(std::min(dur, 2.0) * sr), 0.35, sr), sr, 90.0, 1600.0, 2);
   const Vec blast = (1.0 - 0.6 * dist) * norm(bright) + 1.1 * norm(mid);

   const Vec roll = bandlimit(rng.normal(n), sr, 40.0, 260.0, 2);
   const Vec att2 = attack(n, sr, at, 0.03 + 0.15 * dist);
   const double rollTau = std::max(0.25 * dur, 0.3);
   const Vec sm = smooth(crng, n, sr, 2.0);
   Vec rollEnv(n);
   for (size_t i = 0; i < n; ++i)
      rollEnv[i] = att2[i] * std::exp(-t[i] / rollTau) * (0.6 + 0.4 * sm[i]);

   const double span = std::max(0.2, 0.85 * dur);
   const int64_t cnt = toInt(static_cast<double>(crng.integers(60, 140)) * span);
   Vec tt;
   for (int64_t i = 0; i < cnt; ++i) {
      const double v = 0.12 + crng.exponential(span * 0.3);
      tt.push_back(v);
   }
   Vec keep;
   for (double v : tt)
      if (v < dur - 0.02)
         keep.push_back(v);
   Vec gr(n, 0.0);
   const Vec w = rng.normal(keep.size());
   for (size_t i = 0; i < keep.size(); ++i)
      gr[static_cast<size_t>(toInt(keep[i] * sr))] += w[i] * std::exp(-(keep[i] - 0.12) / (span * 0.45));
   const double d1 = crng.uniform(1800, 2600), d2 = crng.uniform(3200, 4400), d3 = crng.uniform(900, 1300);
   Vec debris = ring(gr, {d1, d2, d3}, {0.012, 0.008, 0.02}, {1.0, 0.6, 0.5}, sr);
   debris = bandlimit(debris, sr, 600.0, 8000.0, 4);

   const Vec tail = fadeOutEnv(n, sr, std::min(0.3 * dur, 1.2));
   const Vec nSub = norm(sub), nRoll = norm(roll), nDebris = norm(debris);
   Vec mono(n);
   for (size_t i = 0; i < n; ++i)
      mono[i] = (0.55 * nSub[i] + blast[i] + 0.45 * nRoll[i] * rollEnv[i] + (0.3 - 0.15 * dist) * nDebris[i]) * tail[i];
   mono = saturate(norm(mono) * 1.2, 1.6);
   const Vec roll2 = bandlimit(rng.normal(n), sr, 40.0, 260.0, 2);
   const Vec nRoll2 = norm(roll2);
   const double level = 1.0 - 0.3 * dist;
   outL.resize(n);
   outR.resize(n);
   for (size_t i = 0; i < n; ++i) {
      const double side = 0.35 * (nRoll2[i] - nRoll[i]) * rollEnv[i] * tail[i];
      outL[i] = (mono[i] + side) * level;
      outR[i] = (mono[i] - side) * level;
   }
}

// ============================================================================
// natural.py
// ============================================================================

void splash(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const int kind = seed % 3;
   const double size = (std::vector<double>{0.35, 0.75, 1.4})[static_cast<size_t>(kind)];
   const double t0 = 0.008;
   const int64_t a0 = toInt(t0 * sr);
   const Vec after = map(t, [&](double v) { return std::max(v - t0, 0.0); });
   Vec slap = waterBand(burst(rng, n, a0, toInt(0.12 * sr), 0.003 + 0.008 * size, sr, 0.0007), sr, 200.0, 9000.0, 2);
   const Vec thump = waterBand(burst(rng, n, a0, toInt(0.35 * sr), 0.015 + 0.05 * size, sr, 0.003), sr, 45.0, 400.0, 2);
   const Vec crownAtt = attack(n, sr, t0, 0.006);
   Vec crownEnv(n);
   for (size_t i = 0; i < n; ++i)
      crownEnv[i] = crownAtt[i] * std::exp(-after[i] / (0.03 + 0.1 * size));
   const Vec crown = waterNoise(rng, n, sr, 700.0, 9000.0) * crownEnv;
   const double tC = t0 + (0.03 + 0.1 * size) * crng.uniform(0.85, 1.15);
   const double rC = (std::vector<double>{5.0, 10.0, 26.0})[static_cast<size_t>(kind)] * crng.uniform(0.85, 1.15);
   const Vec plunk = waterBubbles(n, sr, {tC}, {3260.0 / rC}, {1.0}, 0.8, 0.25, 2);
   const int64_t gk = toInt((25 + 70 * size) * crng.uniform(0.8, 1.2));
   Vec gt(static_cast<size_t>(gk));
   for (double &v : gt)
      v = tC - 0.01 + crng.gamma(1.5, 0.05 + 0.08 * size);
   const Vec gr = waterRadii(crng, static_cast<size_t>(gk), 1.0, 4.0 + 6.0 * size, 2.0);
   Vec gf(gr.size()), ga(gr.size());
   for (size_t i = 0; i < gr.size(); ++i) {
      gf[i] = 3260.0 / gr[i];
      ga[i] = std::pow(gr[i], 0.7) * std::exp(-(gt[i] - tC) / (0.15 + 0.2 * size));
   }
   const Vec gurgle = waterBubbles(n, sr, gt, gf, ga, 1.1, 0.15);
   const double tP = 0.06 + 0.08 * size;
   Vec hump(n);
   for (size_t i = 0; i < n; ++i) {
      const double u = after[i] / tP;
      hump[i] = (u < 1.0 ? u * u : std::exp(-(after[i] - tP) / (0.1 + 0.25 * size))) * (t[i] > t0 + 0.02 ? 1.0 : 0.0);
   }
   if (kind == 2) {
      const double t2 = t0 + crng.uniform(0.32, 0.42);
      for (size_t i = 0; i < n; ++i) {
         const double u2 = std::max(t[i] - t2, 0.0) / 0.25;
         hump[i] = hump[i] + 0.7 * u2 * std::exp(1.0 - u2) * (t[i] > t2 ? 1.0 : 0.0);
      }
      slap = slap + 0.35 * waterBand(burst(rng, n, toInt(t2 * sr), toInt(0.1 * sr), 0.012, sr, 0.002), sr, 300.0, 8000.0, 2);
   }
   const Vec rate = (250.0 + 500.0 * size) * hump;
   const Vec pat = waterCrackle(rng, crng, n, sr, map(rate, [](double v) { return v + 0.5; }), 600.0, 9000.0, 1.5);
   const Vec dt = waterTimes(crng, 0.35 * rate, sr);
   const Vec dr = waterRadii(crng, dt.size(), 0.5, 2.0, 2.0);
   const Vec dropAmp = crng.uniform(0.2, 1.0, dt.size()) * interp(dt, t, hump);
   const Vec drops = waterBubbles(n, sr, dt, map(dr, [](double v) { return 3260.0 / v; }), dropAmp, 1.0, 0.2);
   const Vec sloshAtt = attack(n, sr, t0, 0.04);
   const Vec sloshN = waterNoise(rng, n, sr, 250.0, 1500.0);
   Vec slosh(n);
   for (size_t i = 0; i < n; ++i)
      slosh[i] = sloshN[i] * sloshAtt[i] * std::exp(-after[i] / (0.12 + 0.25 * size));
   const Vec nSlap = norm(slap), nThump = norm(thump), nPlunk = norm(plunk), nGurgle = norm(gurgle), nPat = norm(pat),
             nDrops = norm(drops);
   const Vec fo = fadeOutEnv(n, sr, std::min(0.2, 0.15 * dur));
   Vec mono(n);
   for (size_t i = 0; i < n; ++i)
      mono[i] = (nSlap[i] + (0.15 + 0.3 * size) * nThump[i] + 0.55 * crown[i] + (0.35 + 0.25 * size) * nPlunk[i] +
                 0.5 * nGurgle[i] + 0.35 * nPat[i] + 0.25 * nDrops[i] + 0.3 * slosh[i]) *
                fo[i];
   outL = mono;
   outR = decorrelate(mono, sr, crng);
}

Vec glassFrag(Generator &crng, double f0, double t60) {
   const double r1 = crng.uniform(1.3, 1.55), r2 = crng.uniform(1.9, 2.4), r3 = crng.uniform(2.6, 3.2),
                r4 = crng.uniform(3.4, 4.2);
   const Vec r = {1.0, r1, r2, r3, r4};
   const Vec g = Vec{1.0, 0.8, 0.6, 0.45, 0.3} * crng.uniform(0.4, 1.0, 5);
   return bellsModes(r * f0, Vec{1.0, 0.8, 0.6, 0.5, 0.4} * t60, g, sr);
}

void glassBreak(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const int kind = seed % 3;
   const double t0 = 0.004;
   const int64_t at0 = toInt(t0 * sr);
   Vec crack(n, 0.0);
   bellsPlace(crack, at0,
              bandlimit(burst(rng, static_cast<size_t>(toInt(0.08 * sr)), 0, toInt(0.07 * sr),
                              (std::vector<double>{0.01, 0.016, 0.012})[static_cast<size_t>(kind)], sr, 0.0003),
                        sr, 500.0, -1, 2, 1024));
   const double fb0 = crng.uniform(1500, 2800), fb1 = crng.uniform(500, 1000), fb2 = crng.uniform(800, 1500);
   const double fb = (std::vector<double>{fb0, fb1, fb2})[static_cast<size_t>(kind)];
   Vec body(n, 0.0);
   bellsPlace(body, at0, bellsModes(Vec{1.0, 2.1, 3.6, 5.2} * fb, {0.08, 0.06, 0.045, 0.035}, {1, 0.7, 0.5, 0.3}, sr));
   Vec thud(n, 0.0);
   bellsPlace(thud, at0,
              bandlimit(burst(rng, static_cast<size_t>(toInt(0.2 * sr)), 0, toInt(0.1 * sr),
                              (std::vector<double>{0.012, 0.01, 0.025})[static_cast<size_t>(kind)], sr, 0.002),
                        sr, 60.0, 500.0, 2, 2048));

   Vec left(n, 0.0), right(n, 0.0);
   Vec ct, ca;
   const int count = (std::vector<int>{45, 60, 45})[static_cast<size_t>(kind)];
   const double land = crng.uniform(0.28, 0.42);
   const double low = std::log((std::vector<double>{2200.0, 1500.0, 1300.0})[static_cast<size_t>(kind)]);
   for (int c = 0; c < count; ++c) {
      const double size = std::pow(crng.random(), 1.6);
      const double f0 = std::exp(std::log(7800.0) * (1.0 - size) + low * size) * crng.uniform(0.9, 1.1);
      const double t60 = (0.025 + 0.3 * size) * crng.uniform(0.7, 1.3);
      const Vec ir = glassFrag(crng, f0, t60);
      double tm;
      if (kind == 1 && crng.random() < 0.7)
         tm = t0 + land + crng.gamma(2.0, 0.035);
      else
         tm = t0 + crng.gamma(1.3, 0.05);
      double gap = crng.uniform(0.04, 0.15) * (0.6 + 0.8 * size);
      const double ratio = crng.uniform(0.45, 0.65);
      double a = crng.uniform(0.4, 1.0) * (0.5 + size) * std::exp(-std::max(0.0, tm - t0 - (kind == 1 ? land : 0.0)) / 0.5);
      const double w = crng.uniform(0.15, 0.85);
      const int64_t bounces = crng.integers(2, 6);
      for (int64_t b = 0; b < bounces; ++b) {
         if (tm > dur - 0.06)
            break;
         const int64_t at = toInt(tm * sr);
         bellsPlace(left, at, ir, a * (1.0 - w));
         bellsPlace(right, at, ir, a * w);
         ct.push_back(tm);
         ca.push_back(a);
         tm += gap * crng.uniform(0.85, 1.15);
         gap *= ratio;
         a *= crng.uniform(0.35, 0.6);
      }
   }
   std::vector<Vec> tiny;
   for (int i = 0; i < 6; ++i) {
      const double f = crng.uniform(6500, 9000);
      const double t60 = crng.uniform(0.008, 0.025);
      tiny.push_back(glassFrag(crng, f, t60));
   }
   const double span = (std::vector<double>{0.5, 0.8, 0.6})[static_cast<size_t>(kind)];
   const int bits = (std::vector<int>{140, 220, 160})[static_cast<size_t>(kind)];
   for (int c = 0; c < bits; ++c) {
      const double off = (kind == 1 && crng.random() < 0.6) ? land : 0.0;
      const double tm = t0 + (off + crng.gamma(1.2, span / 4.0));
      if (tm > dur - 0.04)
         continue;
      const double a = crng.uniform(0.1, 0.35) * std::exp(-(tm - t0) / (1.2 * span));
      const double w = crng.uniform(0.1, 0.9);
      const Vec &ir = tiny[static_cast<size_t>(crng.integers(0, 6))];
      bellsPlace(left, toInt(tm * sr), ir, a * (1.0 - w));
      bellsPlace(right, toInt(tm * sr), ir, a * w);
      ct.push_back(tm);
      ca.push_back(0.5 * a);
   }
   Vec clicks = bellsClicks(n, sr, ct, ca, 2500.0, rng);
   const double m = std::max({absMax(left), absMax(right), 1e-12});
   clicks = 0.9 * clicks / std::max(absMax(clicks), 1e-12);
   const Vec base = 0.9 * norm(crack) + 0.3 * norm(body) + (kind == 2 ? 0.45 : 0.25) * norm(thud) + clicks;
   outL = base + 1.4 * left / m;
   outR = base + 1.4 * right / m;
}

void mouseClick(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   Vec out(n, 0.0);
   const int64_t sw = crng.integers(0, int64_t(1) << 31);
   const int64_t shell = crng.integers(0, int64_t(1) << 31);
   Vec presses = {0.004};
   if (seed % 2 == 1)
      presses.push_back(0.004 + crng.uniform(0.11, 0.16));
   for (double tm : presses) {
      const double hold = seed % 2 == 0 ? crng.uniform(0.06, 0.1) : crng.uniform(0.045, 0.07);
      const double rels[2] = {0.0, hold}, lvls[2] = {1.0, 0.55};
      for (int up = 0; up < 2; ++up) {
         const int64_t at = toInt((tm + rels[up]) * sr);
         const Vec snap = deskHit(sr, rng, 0.008, 2500.0, 9500.0, 0.0018, 16, 0.00005, 1.5, 0.00025, sw + up);
         const Vec body = deskHit(sr, rng, 0.02, 900.0, 3500.0, 0.005, 10, 0.0003, 0.4, 0.0008, shell + up);
         const double a = lvls[up] * crng.uniform(0.85, 1.0);
         deskAdd(out, at, snap, a);
         deskAdd(out, at + toInt(crng.uniform(0.0003, 0.0009) * sr), snap, 0.4 * a);
         deskAdd(out, at, body, (up == 0 ? 0.45 : 0.25) * a);
      }
   }
   outL = out;
   outR = out;
}

void cameraShutter(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const int kind = seed % 3;
   Vec out(n, 0.0);
   const int64_t mech = crng.integers(0, int64_t(1) << 31);
   const DeskBody body{300.0, 2500.0, 0.012, 12, 0.7};

   const auto clack = [&](int64_t at, double a, int which, double bright) {
      const Vec g = deskHit(sr, rng, 0.035, 1200.0 * bright, 9000.0, 0.004, 18, 0.00015, 1.5, 0.0006, mech + which, &body);
      deskAdd(out, at, g, a);
      deskAdd(out, at + toInt(crng.uniform(0.002, 0.005) * sr), g, 0.3 * a);
   };
   const auto whirr = [&](int64_t at, double length) {
      const int64_t k = toInt(length * sr);
      if (at >= static_cast<int64_t>(n) || k < 8)
         return;
      const size_t K = static_cast<size_t>(k);
      const double rate = crng.uniform(160.0, 260.0);
      const Vec sm = smooth(rng, K, sr, rate);
      const Vec noise = deskNoise(rng, K, sr, 500.0, 4500.0);
      Vec w(K);
      for (size_t i = 0; i < K; ++i) {
         const double u = static_cast<double>(i) / static_cast<double>(K);
         const double env = std::pow(std::sin(kPi * u), 0.6);
         w[i] = noise[i] * (0.6 + 0.4 * sm[i]) * env;
      }
      deskAdd(out, at, norm(w), 0.18);
   };

   if (kind == 1) {
      double tm = 0.004;
      const double fps = crng.uniform(6.0, 8.0);
      while (tm < dur - 0.13) {
         const int64_t at = toInt(tm * sr);
         const double sp = crng.uniform(0.035, 0.05);
         const double a1 = crng.uniform(0.9, 1.0);
         clack(at, a1, 0, 1.0);
         const double a2 = crng.uniform(0.8, 0.9);
         clack(at + toInt(sp * sr), a2, 1, 0.8);
         const double len = crng.uniform(0.06, 0.08);
         whirr(at + toInt((sp + 0.006) * sr), len);
         tm += 1.0 / fps * crng.uniform(0.99, 1.01);
      }
   } else if (kind == 0) {
      const double sp = crng.uniform(0.04, 0.07);
      clack(toInt(0.004 * sr), 1.0, 0, 1.0);
      clack(toInt((0.004 + sp) * sr), 0.85, 1, 0.8);
      const double len = crng.uniform(0.07, 0.1);
      whirr(toInt((0.01 + sp) * sr), len);
   } else {
      const double sp = crng.uniform(0.012, 0.025);
      clack(toInt(0.004 * sr), 0.9, 0, 1.4);
      clack(toInt((0.004 + sp) * sr), 0.6, 1, 1.4);
      const double st = 0.004 + crng.uniform(0.18, 0.26);
      const double spanA = crng.uniform(0.2, 0.28);
      const double span = std::min(spanA, std::max(0.05, dur - st - 0.08));
      double tt = st;
      while (tt < st + span) {
         const double u = (tt - st) / span;
         const Vec g = deskHit(sr, rng, 0.01, 2500.0, 9000.0, 0.0018, 10, 0.00008, 1.0, 0.0003);
         deskAdd(out, toInt(tt * sr), g, 0.22 * crng.uniform(0.6, 1.0));
         tt += crng.uniform(0.007, 0.02) * (1.2 - 0.6 * std::sin(kPi * u));
      }
      const int64_t a0 = toInt(st * sr), k = toInt(span * sr);
      if (a0 < static_cast<int64_t>(n)) {
         Vec env(n, 0.0);
         const size_t m = static_cast<size_t>(std::min<int64_t>(k, static_cast<int64_t>(n) - a0));
         const Vec l = linspace(0.0, 1.0, m);
         for (size_t i = 0; i < m; ++i)
            env[static_cast<size_t>(a0) + i] = std::sin(kPi * l[i]);
         out += 0.07 * (deskNoise(rng, n, sr, 800.0, 6000.0) * env);
      }
      const double late = crng.uniform(0.01, 0.03);
      clack(toInt((st + span + late) * sr), 0.35, 2, 1.6);
   }
   outL = out;
   outR = decorrelate(out, sr, crng, 3, 0.006);
}

void corkPop(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const bool champ = seed % 2 == 1;
   const double t0 = std::min(0.3 * dur, !champ ? crng.uniform(0.12, 0.2) : crng.uniform(0.03, 0.06));
   const int64_t at = toInt(t0 * sr);
   const Vec tt = map(t, [&](double v) { return std::max(v - t0, 0.0); });
   Vec mono(n, 0.0);

   if (!champ) {
      const int64_t squeaks = crng.integers(2, 4);
      for (int64_t q = 0; q < squeaks; ++q) {
         const double s0 = crng.uniform(0.0, std::max(0.01, t0 - 0.06));
         const double L = crng.uniform(0.02, 0.05);
         const int64_t k = toInt(L * sr);
         const int64_t a0 = toInt(s0 * sr);
         if (a0 + k >= static_cast<int64_t>(n) || k < 8)
            continue;
         const size_t K = static_cast<size_t>(k);
         const Vec u = arangeOverN(K);
         const double r = crng.uniform(250, 450);
         const Vec rate = map(u, [&](double v) { return r * (1.0 + 0.3 * v); });
         const Vec ph = cumsum(rate) / static_cast<double>(sr);
         Vec pul(K, 0.0);
         double prev = 0.0;
         for (size_t i = 0; i < K; ++i) {
            const double f = std::floor(ph[i]);
            if (f - prev > 0)
               pul[i] = 1.0;
            prev = f;
         }
         const double g = crng.uniform(0.5, 1.0);
         Vec sq(n, 0.0);
         for (size_t i = 0; i < K; ++i)
            sq[static_cast<size_t>(a0) + i] = pul[i] * std::sin(kPi * u[i]) * g;
         mono += 0.12 * norm(fftconv(sq, modesIr({1400.0, 2300.0, 3300.0}, {0.006, 0.004, 0.003}, {1.0, 0.6, 0.3}, sr, n), n));
      }
   }

   const double w = champ ? 0.0005 : 0.0009;
   const Vec push = pulse(n, at, std::max<int64_t>(2, toInt(w * sr))) * (!champ ? -1.0 : 1.0);
   const double fa = crng.uniform(330, 480), fbv = crng.uniform(420, 600);
   const double f0 = champ ? fbv : fa;
   Vec f(n);
   for (size_t i = 0; i < n; ++i)
      f[i] = f0 * (0.6 + 0.4 * (1.0 - std::exp(-tt[i] / 0.004)));
   const double d = kPi * f0 / 3.0;
   const Vec cs = cumsum(f);
   const Vec att = attack(n, sr, t0, 0.0004);
   Vec ringv(n);
   for (size_t i = 0; i < n; ++i)
      ringv[i] = std::sin(kTau * cs[i] / sr) * std::exp(-d * tt[i]) * (t[i] >= t0 ? 1.0 : 0.0) * att[i];
   const Vec pop = norm(ringv) + 0.5 * norm(bandlimit(push, sr, 80.0, -1, 2));
   const Vec tick =
      bandlimit(burst(rng, n, at, toInt(0.02 * sr), champ ? 0.003 : 0.002, sr, 0.0004), sr, 300.0, 4000.0, 2);
   mono += pop + (champ ? 0.7 : 0.35) * norm(tick);

   if (champ) {
      const double gt = crng.uniform(0.3, 0.5);
      Vec gEnv(n);
      for (size_t i = 0; i < n; ++i)
         gEnv[i] = (1.0 - std::exp(-tt[i] / 0.02)) * std::exp(-tt[i] / gt) * (t[i] >= t0 ? 1.0 : 0.0);
      const Vec gush = bandlimit(rng.normal(n), sr, 1500.0, 7000.0, 2) * gEnv;
      Vec rate(n);
      for (size_t i = 0; i < n; ++i)
         rate[i] = 2500.0 * std::exp(-tt[i] / 0.9) * (t[i] >= t0 + 0.02 ? 1.0 : 0.0);
      const double total = sum(rate);
      const int64_t cnt = toInt(total / sr) + 1;
      const Vec prob = rate / total;
      const std::vector<int64_t> idx = rng.choiceWeighted(prob, static_cast<size_t>(cnt));
      Vec tms(idx.size());
      for (size_t i = 0; i < idx.size(); ++i)
         tms[i] = static_cast<double>(idx[i]) / sr;
      const Vec amps = map(crng.uniform(0.1, 1.0, static_cast<size_t>(cnt)), [](double v) { return v * v; });
      const Vec fz = grains(rng, n, tms, amps, sr);
      const Vec fizz = bandlimit(fz, sr, 2500.0, 8000.0, 2);
      mono = 1.4 * mono + 0.22 * norm(gush) + 0.18 * norm(fizz);
   }
   outL = mono;
   outR = decorrelate(mono, sr, crng);
}

Vec coinIr(Generator &crng, double size, double t60) {
   const double f1 = crng.uniform(5000, 6200) / size;
   const Vec freqs = (Vec{1.0, 1.73, 2.33} * f1) * crng.uniform(0.99, 1.01, 3);
   Vec fa, fb, ta, tb;
   bellsDoublets(freqs, Vec{1.0, 0.7, 0.5} * t60, crng, 0.004, fa, fb, ta, tb);
   const Vec g = {1.0, 0.55, 0.45};
   const Vec g2 = g * crng.uniform(0.5, 1.0, 3);
   return bellsModes(concat(fa, fb), concat(ta, tb), concat(g, g2), sr);
}

void coinDrop(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const int kind = seed % 3;
   const Vec t = times(n, sr);
   const Vec floorIr = bellsModes(Vec{1.0, 2.2, 3.7} * crng.uniform(300, 500), {0.05, 0.035, 0.025}, {1, 0.6, 0.4}, sr);
   Vec clicksT, clicksA;
   Vec mono(n, 0.0);

   const auto settle = [&](double tStart, double a0, double length, double r0, double rmax, Vec &exc) -> double {
      const int64_t i0 = toInt(tStart * sr);
      const int64_t i1 = std::min<int64_t>(static_cast<int64_t>(n) - toInt(0.03 * sr), toInt((tStart + length) * sr));
      if (i1 - i0 < 10)
         return tStart;
      const size_t m = static_cast<size_t>(i1 - i0);
      const Vec tt = times(m, sr);
      Vec rate(m);
      for (size_t i = 0; i < m; ++i)
         rate[i] = std::min(rmax, r0 * std::pow(length / std::max(length - tt[i], 1e-3), 0.6));
      const Vec ph = cumsum(rate) / static_cast<double>(sr);
      std::vector<size_t> idx;
      for (size_t i = 1; i < m; ++i)
         if (std::floor(ph[i]) - std::floor(ph[i - 1]) > 0)
            idx.push_back(i);
      const Vec u = crng.uniform(0.5, 1.0, idx.size());
      for (size_t q = 0; q < idx.size(); ++q) {
         const double x = tt[idx[q]] / length;
         const double amp = a0 * (0.5 + 0.5 * (x * x)) * u[q];
         exc[static_cast<size_t>(i0) + idx[q]] += amp;
         clicksT.push_back(tStart + tt[idx[q]]);
         clicksA.push_back(0.6 * amp);
      }
      return tStart + tt[m - 1];
   };

   if (kind == 0 || kind == 1) {
      const double size = crng.uniform(0.9, 1.25);
      const double ft60 = crng.uniform(0.5, 0.9);
      const Vec freeIr = coinIr(crng, size, ft60);
      const double dt60 = crng.uniform(0.04, 0.07);
      const Vec damped = coinIr(crng, size, dt60);
      Vec excFree(n, 0.0), excDamp(n, 0.0);
      double tSet, end;
      if (kind == 0) {
         double tm = 0.005, a = 1.0;
         double gap = crng.uniform(0.16, 0.26);
         const double ratio = crng.uniform(0.5, 0.62);
         const int64_t bounces = crng.integers(4, 7);
         for (int64_t b = 0; b < bounces; ++b) {
            if (tm > dur - 0.3)
               break;
            const double dt2 = crng.uniform(0.003, 0.012), f2 = crng.uniform(0.3, 0.6);
            const double dts[2] = {0.0, dt2}, fs[2] = {1.0, f2};
            for (int q = 0; q < 2; ++q) {
               const int64_t at = toInt((tm + dts[q]) * sr);
               excFree[static_cast<size_t>(at)] += a * fs[q];
               clicksT.push_back(tm + dts[q]);
               clicksA.push_back(a * fs[q]);
            }
            tm += gap * crng.uniform(0.9, 1.1);
            gap *= ratio;
            a *= crng.uniform(0.55, 0.75);
            if (gap < 0.025)
               break;
         }
         tSet = tm;
         const double len = std::min(crng.uniform(0.35, 0.7), dur - tSet - 0.08);
         const double r0 = crng.uniform(18, 30);
         end = settle(tSet, 0.5 * a + 0.25, len, r0, 140.0, excDamp);
      } else {
         excFree[static_cast<size_t>(toInt(0.005 * sr))] = 0.8;
         clicksT.push_back(0.005);
         clicksA.push_back(0.8);
         tSet = 0.02;
         const double T = crng.uniform(0.7, 0.85) * dur;
         const double r0 = crng.uniform(16, 24);
         end = settle(0.05, 0.3, T - 0.05, r0, 240.0, excDamp);
         const size_t e = static_cast<size_t>(toInt(end * sr));
         for (size_t i = 0; i < e && i < n; ++i)
            excFree[i] += 0.25 * excDamp[i];
      }
      Vec ringv = fftconv(excFree, freeIr, n);
      for (size_t i = 0; i < n; ++i) {
         ringv[i] *= t[i] < tSet ? 1.0 : std::exp(-std::max(t[i] - tSet, 0.0) / 0.12);
         ringv[i] *= t[i] < end ? 1.0 : std::exp(-std::max(t[i] - end, 0.0) / 0.015);
      }
      const int64_t at = toInt(end * sr) + 1;
      if (at < static_cast<int64_t>(n)) {
         excDamp[static_cast<size_t>(at)] += 0.5;
         clicksT.push_back(end);
         clicksA.push_back(0.6);
      }
      mono = ringv + fftconv(excDamp, damped, n);
      const Vec body = fftconv(excFree + excDamp, floorIr, n);
      mono = mono + 0.25 * body * absMax(mono) / std::max(absMax(body), 1e-12);
   } else {
      const Vec dish = bellsModes(Vec{1.0, 2.2, 3.6, 5.1} * crng.uniform(900, 1300), {0.35, 0.25, 0.15, 0.1},
                                  {1, 0.6, 0.4, 0.25}, sr);
      const int64_t k = crng.integers(6, 11);
      std::vector<Vec> coins;
      for (int64_t c = 0; c < k; ++c) {
         const double size = crng.uniform(0.85, 1.3);
         const double t60 = crng.uniform(0.15, 0.35);
         coins.push_back(coinIr(crng, size, t60));
      }
      Vec excDish(n, 0.0);
      for (int64_t c = 0; c < k; ++c) {
         double tm = 0.005 + crng.gamma(1.5, 0.035);
         double a = crng.uniform(0.5, 1.0);
         double gap = crng.uniform(0.03, 0.08);
         const int64_t bounces = crng.integers(2, 5);
         for (int64_t b = 0; b < bounces; ++b) {
            if (tm > dur - 0.05)
               break;
            const int64_t at = toInt(tm * sr);
            bellsPlace(mono, at, coins[static_cast<size_t>(c)], a);
            if (crng.random() < 0.5)
               bellsPlace(mono, at, coins[static_cast<size_t>(crng.integers(0, k))], 0.5 * a);
            else
               excDish[static_cast<size_t>(at)] += a;
            clicksT.push_back(tm);
            clicksA.push_back(a);
            tm += gap * crng.uniform(0.8, 1.2);
            gap *= 0.55;
            a *= crng.uniform(0.35, 0.6);
         }
      }
      const int64_t late = crng.integers(3, 7);
      for (int64_t q = 0; q < late; ++q) {
         const double tm = crng.uniform(0.3, std::min(1.2, dur - 0.1));
         const double a = crng.uniform(0.05, 0.15);
         bellsPlace(mono, toInt(tm * sr), coins[static_cast<size_t>(crng.integers(0, k))], a);
         clicksT.push_back(tm);
         clicksA.push_back(a);
      }
      const Vec d = fftconv(excDish, dish, n);
      mono = mono + 0.35 * d * absMax(mono) / std::max(absMax(d), 1e-12);
   }
   const Vec clicks = bellsClicks(n, sr, clicksT, clicksA, 2000.0, rng);
   mono = mono / std::max(absMax(mono), 1e-12) + 0.35 * norm(clicks);
   outL = mono;
   outR = decorrelate(mono, sr, crng);
}

void metalClang(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const int kind = seed % 3;
   const int64_t at = toInt(0.003 * sr);
   Vec freqs, t60, g;
   if (kind == 0) {
      const double f = crng.uniform(380, 620);
      freqs = Vec{1.0, 2.756, 5.404, 8.933, 13.34} * f;
      t60 = {2.2, 1.6, 1.0, 0.6, 0.35};
      g = {1.0, 0.8, 0.55, 0.35, 0.2};
   } else if (kind == 1) {
      const double f = crng.uniform(180, 300);
      Vec r = crng.uniform(1.0, 18.0, 14);
      std::sort(r.begin(), r.end());
      freqs = f * r;
      t60 = crng.uniform(0.3, 0.9, 14);
      g = crng.uniform(0.3, 1.0, 14);
   } else {
      const double f = crng.uniform(900, 1200);
      freqs = Vec{1.0, 1.58, 2.33, 3.1, 4.4} * f;
      t60 = {2.8, 1.8, 1.1, 0.7, 0.4};
      g = {1.0, 0.6, 0.45, 0.3, 0.2};
   }
   const Vec jitter = crng.uniform(0.99, 1.01, freqs.size());
   const Vec ringv = strike(n, sr, at, freqs * jitter, t60, g, rng, 0.0004, 0.4);
   const Vec hit = bandlimit(burst(rng, n, at, toInt(0.02 * sr), 0.003, sr, 0.0005), sr, 400.0, 8000.0, 4);
   const Vec mono = norm(ringv) + 0.35 * norm(hit);
   const double bf = crng.uniform(1.5, 4.0);
   Vec beat(n);
   for (size_t i = 0; i < n; ++i)
      beat[i] = 1.0 + 0.15 * std::sin(kTau * bf * static_cast<double>(i) / sr);
   outL = mono * beat;
   outR = decorrelate(mono, sr, crng) * beat;
}

void heartbeat(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const int kind = seed % 3;
   const double bpm = (std::vector<double>{62.0, 88.0, 120.0})[static_cast<size_t>(kind)] * crng.uniform(0.97, 1.03);
   const double period = 60.0 / bpm;
   const double systole = std::max(0.2, 0.5 - 0.0024 * bpm);
   const double resp = crng.uniform(3.5, 5.0);
   const double rph = crng.uniform(0.0, kTau);
   const double rsa = (std::vector<double>{0.04, 0.025, 0.012})[static_cast<size_t>(kind)];
   const double fS1 = crng.uniform(34, 44), fS2 = crng.uniform(50, 62);
   Vec mono(n, 0.0), tex(n, 0.0);

   const auto thump = [&](int64_t at, double f, double tau, double amp) {
      const int64_t k = std::min<int64_t>(static_cast<int64_t>(n) - at, toInt(7 * tau * sr));
      if (k < 10)
         return;
      const size_t K = static_cast<size_t>(k);
      const Vec tt = times(K, sr);
      Vec env(K), fr(K);
      for (size_t i = 0; i < K; ++i) {
         env[i] = (1.0 - std::exp(-tt[i] / 0.004)) * std::exp(-tt[i] / tau);
         fr[i] = f * (1.0 + 0.15 * std::exp(-tt[i] / 0.02));
      }
      const Vec cs = cumsum(fr);
      for (size_t i = 0; i < K; ++i) {
         const double ph = kTau * cs[i] / sr;
         const double y = std::sin(ph) * env[i] + 0.45 * std::sin(2.3 * ph + 0.7) * env[i] * std::exp(-tt[i] / (0.5 * tau));
         mono[static_cast<size_t>(at) + i] += amp * y;
         tex[static_cast<size_t>(at) + i] += amp * env[i];
      }
   };

   double tm = 0.03;
   while (tm < dur - 0.2) {
      const double insp = 0.5 + 0.5 * std::sin(kTau * tm / resp + rph);
      const double a = crng.uniform(0.88, 1.0) * (1.0 - 0.08 * insp);
      {
         const double f = fS1 * crng.uniform(0.95, 1.05);
         const double tau = crng.uniform(0.022, 0.03);
         thump(toInt(tm * sr), f, tau, a);
      }
      {
         const int64_t at = toInt((tm + crng.uniform(0.02, 0.035)) * sr);
         const double f = fS1 * crng.uniform(1.1, 1.25);
         thump(at, f, 0.02, 0.55 * a);
      }
      const double t2 = tm + systole * crng.uniform(0.98, 1.02);
      if (t2 < dur - 0.1) {
         const double f = fS2 * crng.uniform(0.95, 1.05);
         const double tau = crng.uniform(0.014, 0.018);
         thump(toInt(t2 * sr), f, tau, 0.75 * a);
         const double split = crng.uniform(0.012, 0.022) + 0.025 * insp;
         const double f2 = fS2 * crng.uniform(1.1, 1.3);
         thump(toInt((t2 + split) * sr), f2, 0.012, 0.4 * a);
      }
      tm += period * (1.0 + rsa * std::sin(kTau * tm / resp + rph)) * crng.uniform(0.99, 1.01);
   }
   const Vec noise = bandlimit(rng.normal(n), sr, 30.0, 250.0, 2);
   Vec m = norm(mono) + 0.25 * norm(noise * tex);
   m = bandlimit(m, sr, 22.0, 400.0, 2);
   outL = m;
   outR = m;
}

void canOpening(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const bool beer = seed % 2 == 1;
   const double tLift = 0.02 + crng.uniform(0.0, 0.03);
   const Vec lift =
      strike(n, sr, toInt(tLift * sr), {3100.0, 4700.0, 6900.0}, {0.006, 0.004, 0.003}, {1, 0.6, 0.4}, rng, 0.0002, 0.8);
   const double tC = tLift + crng.uniform(0.07, 0.14);
   const int64_t ac = toInt(tC * sr);
   const Vec lid = Vec{2600.0, 4100.0, 5600.0, 7300.0} * crng.uniform(0.92, 1.08);
   const Vec crack = strike(n, sr, ac, lid, {0.025, 0.018, 0.012, 0.008}, {1, 0.8, 0.6, 0.4}, rng, 0.00015, 1.5);
   const Vec snap = waterBand(burst(rng, n, ac, toInt(0.006 * sr), 0.0012, sr, 0.0003), sr, 800.0, 10000.0, 2);
   const double tau = beer ? 0.13 : 0.08;
   const Vec att = attack(n, sr, tC, 0.0015);
   Vec env(n);
   for (size_t i = 0; i < n; ++i) {
      const double aft = std::max(t[i] - tC, 0.0);
      const double q = aft / (2.2 * tau);
      env[i] = att[i] * (std::exp(-(q * q)) * (0.6 + 0.4 * std::exp(-aft / tau)) + 0.03 * std::exp(-aft / (4.0 * tau)));
   }
   const Vec flick = map(smooth(crng, n, sr, 60.0), [](double v) { return 0.75 + 0.25 * v; });
   const Vec hiss = waterBand(rng.normal(n), sr, 700.0, 10000.0, 1) * env * flick;
   const Vec spit = waterCrackle(rng, crng, n, sr, map(env, [](double v) { return 2500.0 * v + 1.0; }), 1500.0, 9000.0, 1.6);
   const double tF = tC + crng.uniform(0.08, 0.16);
   const Vec fold =
      strike(n, sr, toInt(tF * sr), {1350.0, 2400.0, 3800.0}, {0.02, 0.015, 0.01}, {1, 0.6, 0.4}, rng, 0.0004, 0.6);
   const double tBack = tF + crng.uniform(0.25, 0.45);
   const Vec back = strike(n, sr, toInt(tBack * sr), {2900.0, 4400.0}, {0.008, 0.005}, {1, 0.6}, rng, 0.0002, 0.5);
   const Vec fzAtt = attack(n, sr, tC + 0.03, 0.08);
   Vec fz(n);
   for (size_t i = 0; i < n; ++i)
      fz[i] = fzAtt[i] * std::exp(-std::max(t[i] - tC, 0.0) / (beer ? 0.9 : 0.6));
   const Vec fizz = waterCrackle(rng, crng, n, sr, map(fz, [](double v) { return 400.0 * v + 1.0; }), 2500.0, 9500.0, 1.5);
   const Vec ft = waterTimes(crng, 350.0 * fz, sr);
   const Vec fbF = crng.uniform(2500.0, 6000.0, ft.size());
   const Vec fbA = crng.uniform(0.2, 1.0, ft.size()) * interp(ft, t, fz);
   const Vec fb = waterBubbles(n, sr, ft, fbF, fbA, 1.2, 0.1);
   const Vec nLift = norm(lift), nCrack = norm(crack), nSnap = norm(snap), nHiss = norm(hiss), nSpit = norm(spit),
             nFold = norm(fold), nBack = norm(back), nFizz = norm(fizz), nFb = norm(fb);
   const Vec fo = fadeOutEnv(n, sr, std::min(0.2, 0.15 * dur));
   Vec mono(n);
   for (size_t i = 0; i < n; ++i)
      mono[i] = (0.25 * nLift[i] + 0.4 * nCrack[i] + 0.35 * nSnap[i] + nHiss[i] + 0.3 * nSpit[i] * env[i] +
                 0.35 * nFold[i] + 0.2 * nBack[i] + 0.05 * nFizz[i] + 0.03 * nFb[i]) *
                fo[i];
   outL = mono;
   outR = decorrelate(mono, sr, crng, 3);
}

Vec punchReso(const Vec &x, double f, double q) {
   const size_t n = x.size();
   const double d = kPi * f / q;
   const size_t k = std::min<size_t>(n, static_cast<size_t>(toInt(7.0 / d * sr) + 2));
   Vec ir(k);
   for (size_t i = 0; i < k; ++i) {
      const double t = static_cast<double>(i) / sr;
      ir[i] = std::exp(-d * t) * std::sin(kTau * f * t);
   }
   return fftconv(x, ir, n);
}

void punch(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const Vec t = times(n, sr);
   const int kind = seed % 3;
   const double c0 = crng.uniform(0.07, 0.1), c1 = crng.uniform(0.06, 0.09), c2 = crng.uniform(0.17, 0.22);
   double t0 = (std::vector<double>{c0, c1, c2})[static_cast<size_t>(kind)];
   t0 = std::min(t0, 0.3 * static_cast<double>(n) / sr);
   const int64_t at = toInt(t0 * sr);

   Vec u(n), swishEnv(n);
   for (size_t i = 0; i < n; ++i) {
      u[i] = clip(t[i] / t0, 0.0, 1.0);
      swishEnv[i] = std::pow(std::sin(0.5 * kPi * u[i]), 3) * (t[i] < t0 ? 1.0 : 0.0);
   }
   const Vec noise = rng.normal(n);
   const Vec swLo = bandlimit(noise, sr, 250.0, 900.0, 2);
   const Vec swHi = bandlimit(noise, sr, 900.0, 3500.0, 2);
   Vec swish(n);
   for (size_t i = 0; i < n; ++i)
      swish[i] = (swLo[i] * (1.0 - u[i]) + swHi[i] * u[i]) * swishEnv[i];

   const double tauS = (std::vector<double>{0.006, 0.004, 0.007})[static_cast<size_t>(kind)];
   const Vec crack = burst(rng, n, at, toInt(0.05 * sr), tauS, sr, 0.0003);
   const double r1 = crng.uniform(1100, 2400);
   const Vec a1 = punchReso(crack, r1, 3.0);
   const double r2 = crng.uniform(450, 750);
   const Vec a2 = punchReso(crack, r2, 2.5);
   const Vec slap = bandlimit(crack, sr, 500.0, 7000.0, 2) + 0.8 * a1 + 0.5 * a2;

   const double width = (std::vector<double>{0.007, 0.004, 0.009})[static_cast<size_t>(kind)];
   const Vec push = pulse(n, at, toInt(width * sr));
   const double b0 = crng.uniform(85, 120), b1 = crng.uniform(150, 210), b2 = crng.uniform(60, 85);
   const double fBody = (std::vector<double>{b0, b1, b2})[static_cast<size_t>(kind)];
   Vec thud = punchReso(push, fBody, 1.8) + 0.6 * punchReso(push, fBody * 2.3, 2.5);
   const Vec knock = bandlimit(
      burst(rng, n, at, toInt(0.12 * sr), (std::vector<double>{0.025, 0.015, 0.04})[static_cast<size_t>(kind)], sr, 0.001),
      sr, 40.0, 400.0, 2);
   thud = norm(thud) + 0.6 * norm(knock);

   const double lv[3][3] = {{1.0, 0.6, 0.15}, {0.7, 1.0, 0.12}, {1.1, 0.9, 0.3}};
   Vec mono = lv[kind][0] * thud + lv[kind][1] * norm(slap) + lv[kind][2] * norm(swish);
   if (kind == 1) {
      const int64_t cnt = crng.integers(5, 12);
      Vec tms(static_cast<size_t>(cnt));
      for (double &v : tms)
         v = t0 + crng.gamma(1.5, 0.004);
      const Vec amps = crng.uniform(0.3, 1.0, static_cast<size_t>(cnt));
      const Vec cr = grains(rng, n, tms, amps, sr);
      mono += 0.25 * norm(bandlimit(cr, sr, 1200.0, 5000.0, 2));
   }
   const Vec settle =
      bandlimit(burst(rng, n, at + toInt(0.01 * sr), toInt(0.15 * sr), 0.04, sr, 0.01), sr, 400.0, 3000.0, 2);
   mono += 0.06 * norm(settle);
   mono = saturate(norm(mono) * (std::vector<double>{1.2, 1.1, 1.6})[static_cast<size_t>(kind)], 1.3);
   outL = mono;
   outR = decorrelate(mono, sr, crng, 3);
}

Vec blade(size_t n, int64_t at, Generator &rng, Generator &crng) {
   const double f = crng.uniform(1100, 1700);
   const Vec freqs = Vec{1.0, 1.37, 2.13, 2.71, 3.66, 4.9} * f;
   const Vec t60 = Vec{1.2, 0.9, 0.7, 0.5, 0.35, 0.25} * crng.uniform(0.8, 1.2);
   return strike(n, sr, at, freqs, t60, {1.0, 0.8, 0.6, 0.45, 0.3, 0.2}, rng, 0.0002, 0.7);
}

void swordClash(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const int kind = seed % 3;
   Vec mono(n, 0.0);
   if (kind == 1) {
      const double L = std::min(0.45, 0.6 * dur);
      const Vec u = map(t, [&](double v) { return clip(v / L, 0.0, 1.0); });
      Vec scrape(n, 0.0);
      const Vec noise = rng.normal(n);
      const size_t blk = static_cast<size_t>(toInt(0.03 * sr));
      for (size_t s = 0; s < n; s += blk) {
         const size_t e = std::min(n, s + 2 * blk);
         const double c = 1800 + 2600 * u[s];
         const Vec seg = bandlimit(slice(noise, s, e), sr, c * 0.8, c * 1.2, 2) * hanning(e - s);
         for (size_t i = 0; i < seg.size(); ++i)
            scrape[s + i] += seg[i];
      }
      Vec env(n);
      for (size_t i = 0; i < n; ++i) {
         const double s = std::sin(0.5 * kPi * u[i]);
         env[i] = (s * s) * std::exp(-std::max(t[i] - L, 0.0) / 0.05);
      }
      const Vec ringv = blade(n, toInt(L * sr), rng, crng);
      mono = 0.8 * norm(scrape) * env + 0.7 * norm(ringv);
   } else {
      Vec hits = {0.004};
      if (kind != 0)
         hits.push_back(crng.uniform(0.18, 0.3));
      for (double h : hits) {
         const int64_t at = toInt(h * sr);
         const Vec ringv = blade(n, at, rng, crng);
         const Vec hit = bandlimit(burst(rng, n, at, toInt(0.03 * sr), 0.004, sr, 0.0005), sr, 1000.0, 9000.0, 4);
         const Vec grind = bandlimit(burst(rng, n, at, toInt(0.15 * sr), 0.04, sr), sr, 2500.0, 7000.0, 4);
         mono += norm(ringv) + 0.5 * norm(hit) + 0.2 * norm(grind);
      }
   }
   outL = mono;
   outR = decorrelate(mono, sr, crng);
}

Vec gunNwave(size_t n, int64_t at, double length) {
   Vec out(n, 0.0);
   const int64_t k = std::max<int64_t>(4, toInt(length * sr));
   const int64_t r = std::max<int64_t>(2, toInt(0.0001 * sr));
   if (at + k + 2 * r >= static_cast<int64_t>(n))
      return out;
   Vec up(static_cast<size_t>(r));
   for (int64_t i = 0; i < r; ++i)
      up[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(r));
   for (int64_t i = 0; i < r; ++i)
      out[static_cast<size_t>(at + i)] = up[static_cast<size_t>(i)];
   const Vec l = linspace(1.0, -1.0, static_cast<size_t>(k));
   for (int64_t i = 0; i < k; ++i)
      out[static_cast<size_t>(at + r + i)] = l[static_cast<size_t>(i)];
   for (int64_t i = 0; i < r; ++i)
      out[static_cast<size_t>(at + r + k + i)] = -up[static_cast<size_t>(r - 1 - i)];
   return out;
}

Vec gunBlast(size_t n, int64_t at, double length, double decay) {
   Vec out(n, 0.0);
   const int64_t k = std::min<int64_t>(static_cast<int64_t>(n) - at, toInt((length * 6 + 0.002) * sr));
   if (k < 8)
      return out;
   const int64_t r = std::max<int64_t>(2, toInt(0.00012 * sr));
   for (int64_t i = 0; i < k; ++i) {
      const double t = static_cast<double>(i) / sr;
      double w = (1.0 - t / length) * std::exp(-decay * t / length);
      if (i < r)
         w *= 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(r));
      out[static_cast<size_t>(at + i)] = w;
   }
   return out;
}

void gunshot(const Event &ev, Generator &rng, Generator &crng, Params &p, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const Vec t = times(n, sr);
   const int kind = seed % 3;
   const double t0 = 0.004;
   const int64_t at = toInt(t0 * sr);
   const double T0 = crng.uniform(0.0011, 0.0016), T1 = crng.uniform(0.0016, 0.0024), T2 = crng.uniform(0.002, 0.003);
   const double T = (std::vector<double>{T0, T1, T2})[static_cast<size_t>(kind)];
   const Vec blast = gunBlast(n, at, T, 2.2);
   const double tau = (std::vector<double>{0.012, 0.018, 0.02})[static_cast<size_t>(kind)];
   const Vec gas = bandlimit(burst(rng, n, at, toInt(0.25 * sr), tau, sr, 0.0003), sr, 180.0, 6500.0, 2);
   Vec src = norm(blast) + (std::vector<double>{0.55, 0.5, 0.45})[static_cast<size_t>(kind)] * norm(gas);
   if (kind == 1) {
      const int64_t lead = toInt(crng.uniform(0.0015, 0.004) * sr);
      const Vec crack = gunNwave(n, at, crng.uniform(0.00025, 0.0004));
      Vec rolled(n);
      for (size_t i = 0; i < n; ++i)
         rolled[(i + static_cast<size_t>(lead)) % n] = src[i];
      src = rolled + 0.8 * crack;
   }
   if (kind == 2)
      src = bandlimit(src, sr, 60.0, crng.uniform(1100.0, 1600.0), 2);
   src = saturate(norm(src) * (std::vector<double>{2.2, 2.6, 1.3})[static_cast<size_t>(kind)], 2.0);
   const size_t g = static_cast<size_t>(toInt(crng.uniform(0.002, 0.006) * sr));
   {
      const Vec dark = bandlimit(src, sr, -1, 4000.0, 2);
      Vec s2(n);
      for (size_t i = 0; i < n; ++i)
         s2[i] = src[i] + 0.5 * (i < g ? 0.0 : dark[i - g]);
      src = s2;
   }
   const double T60 = (std::vector<double>{0.8, 1.0, 1.6})[static_cast<size_t>(kind)] * crng.uniform(0.9, 1.1);
   const double rollLp = (std::vector<double>{2600.0, 2200.0, 1100.0})[static_cast<size_t>(kind)];
   const double rollLvl = (std::vector<double>{0.1, 0.12, 0.35})[static_cast<size_t>(kind)];

   const auto space = [&](Generator &r) {
      Vec er(n, 0.0);
      const int64_t kEcho = crng.integers(4, 7);
      for (int64_t i = 0; i < kEcho; ++i) {
         const double d = crng.uniform(0.04, kind != 2 ? 0.55 : 0.9);
         const int64_t dd = toInt(d * sr);
         if (dd < static_cast<int64_t>(n)) {
            const double gain = crng.uniform(0.12, 0.3) * std::exp(-d / 0.4);
            for (size_t q = static_cast<size_t>(dd); q < n; ++q)
               er[q] += gain * src[q - static_cast<size_t>(dd)];
         }
      }
      er = bandlimit(er, sr, 90.0, kind != 2 ? 2400.0 : 1000.0, 2);
      Vec env(n);
      for (size_t i = 0; i < n; ++i) {
         const double a = std::max(t[i] - t0, 0.0);
         env[i] = (1.0 - std::exp(-a / (kind != 2 ? 0.03 : 0.08))) * std::exp(-kLn1000 * a / T60);
      }
      Vec roll = bandlimit(r.normal(n), sr, 70.0, rollLp, 2) * env;
      const Vec sm = smooth(crng, n, sr, 6.0);
      for (size_t i = 0; i < n; ++i)
         roll[i] *= 0.7 + 0.3 * sm[i];
      return er + rollLvl * norm(roll);
   };

   const Vec direct = src * (kind != 2 ? 1.0 : 0.6);
   p["reverb"] = (std::vector<double>{0.15, 0.18, 0.3})[static_cast<size_t>(kind)];
   p["reverb_decay"] = (std::vector<double>{1.0, 1.2, 1.8})[static_cast<size_t>(kind)];
   const Vec fo = fadeOutEnv(n, sr, std::min(0.25, 0.3 * dur));
   outL = (direct + space(rng)) * fo;
   outR = (direct + space(crng)) * fo;
}

void churchBell(const Event &ev, Generator &rng, Generator &crng, Params &, int seed, Vec &outL, Vec &outR) {
   const size_t n = lengthOf(ev);
   const double dur = static_cast<double>(n) / sr;
   const double fN = crng.uniform(520, 980);
   Vec ratios = {0.25, 0.5, 0.6, 0.75, 1.0, 1.25, 1.335, 1.5, 2.0, 2.5, 2.67, 3.0, 3.36, 4.0, 4.5, 5.0};
   ratios = ratios * crng.uniform(0.993, 1.007, ratios.size());
   ratios[0] *= crng.uniform(0.97, 1.03);
   ratios[1] *= crng.uniform(0.98, 1.02);
   ratios[4] = 1.0;
   const double size = std::pow(700.0 / fN, 0.8);
   const Vec t60 = Vec{24, 12, 9, 6, 6, 3, 3, 3.5, 2.6, 1.8, 1.6, 1.4, 1.1, 0.9, 0.7, 0.55} * size;
   const Vec g = Vec{0.35, 0.5, 0.75, 0.25, 1.0, 0.2, 0.3, 0.55, 0.5, 0.4, 0.35, 0.3, 0.25, 0.2, 0.12, 0.08} *
                 crng.uniform(0.75, 1.25, 16);
   Vec fa, fb, ta, tb;
   bellsDoublets(fN * ratios, t60, crng, 0.0012, fa, fb, ta, tb);
   const Vec uL = crng.uniform(0.25, 0.75, 16);
   const Vec uJ = crng.uniform(-0.2, 0.2, 16);
   Vec uR(16);
   for (size_t i = 0; i < 16; ++i)
      uR[i] = clip(uL[i] + uJ[i], 0.1, 0.9);
   Vec irL(n, 0.0), irR(n, 0.0);
   for (size_t i = 0; i < 16; ++i) {
      const double fs[2] = {fa[i], fb[i]}, ds[2] = {ta[i], tb[i]}, uls[2] = {uL[i], 1.0 - uL[i]},
                   urs[2] = {uR[i], 1.0 - uR[i]};
      for (int h = 0; h < 2; ++h) {
         const size_t k = std::min<size_t>(n, static_cast<size_t>(toInt(1.1 * ds[h] * sr) + 1));
         const Vec mode = bellsMode(fs[h], ds[h], sr, k);
         for (size_t q = 0; q < k; ++q) {
            const double m = g[i] * mode[q];
            irL[q] += uls[h] * m;
            irR[q] += urs[h] * m;
         }
      }
   }
   Vec strikes;
   if (seed % 2 == 0) {
      strikes = {0.005};
   } else {
      const double stop = std::max(0.01, dur - 0.5);
      const size_t count = static_cast<size_t>(std::ceil((stop - 0.005) / 2.5));
      for (size_t i = 0; i < count; ++i)
         strikes.push_back(0.005 + static_cast<double>(i) * 2.5);
   }
   const Vec amps = crng.uniform(0.85, 1.0, strikes.size());
   const size_t k = static_cast<size_t>(toInt(0.05 * sr));
   Vec clank(n, 0.0);
   for (double st : strikes)
      bellsPlace(clank, toInt(st * sr), bandlimit(burst(rng, k, 0, toInt(0.02 * sr), 0.003, sr), sr, 1000.0, 6000.0, 2, 1024));
   const Vec fo = fadeOutEnv(n, sr, std::min(1.5, 0.3 * dur));
   const Vec nClank = norm(clank);
   Vec outs[2];
   const Vec *irs[2] = {&irL, &irR};
   for (int c = 0; c < 2; ++c) {
      Vec y(n, 0.0);
      for (size_t s = 0; s < strikes.size(); ++s)
         bellsPlace(y, toInt(strikes[s] * sr), *irs[c], amps[s]);
      outs[c] = (norm(y) + 0.05 * nClank) * fo;
   }
   outL = outs[0];
   outR = outs[1];
}

// ============================================================================
// the role table
// ============================================================================

using EventFn = Vec (*)(const Event &, Generator &, const Params &);

struct Role {
   const char *name;
   double apex; // grammar.EVENT_STYLE's apex fraction
   EventFn event = nullptr;
   Foley foley = nullptr;
   Params params;
};

const std::vector<Role> &roles() {
   static const std::vector<Role> table = {
      {"whoosh", 0.05, whoosh, nullptr,
       {{"f_start", 180}, {"f_peak", 2400}, {"f_end", 500}, {"body_f", 90}, {"width", 0.55}, {"reverb", 0.15}}},
      {"impact", 0.0, impact, nullptr, {{"f0", 70}, {"t60", 0.35}, {"width", 0.0}, {"reverb", 0.2}}},
      {"ui_pop", 0.0, uiPop, nullptr, {{"f0", 880}, {"t60", 0.07}, {"width", 0.0}, {"reverb", 0.08}}},
      {"riser", 0.92, riser, nullptr,
       {{"f_start", 220}, {"f_peak", 6500}, {"body_f", 110}, {"width", 0.45}, {"reverb", 0.25}}},
      {"swell", 0.90, swell, nullptr, {{"f_start", 350}, {"f_peak", 3200}, {"width", 0.6}, {"reverb", 0.35}}},
      {"click", 0.0, click, nullptr, {{"f0", 1900}, {"t60", 0.03}, {"width", 0.0}, {"reverb", 0.05}}},
      {"bell", 0.10, bell, nullptr, {{"f0", 880}, {"t60", 1.2}, {"width", 0.3}, {"reverb", 0.3}}},
      {"sub_drop", 0.10, subDrop, nullptr, {{"f0", 110}, {"f_end", 35}, {"t60", 0.8}, {"width", 0.0}, {"reverb", 0.1}}},
      {"glitch", 0.0, glitch, nullptr, {{"f0", 600}, {"t60", 0.2}, {"width", 0.5}, {"reverb", 0.05}}},
      {"noise_sweep", 0.50, noiseSweep, nullptr,
       {{"f_start", 300}, {"f_peak", 4200}, {"f_end", 800}, {"width", 0.5}, {"reverb", 0.25}}},
      {"thunder", 0.02, nullptr, nullptr, {}},
      {"door_slam", 0.0, nullptr, doorSlam, {{"f0", 95.0}, {"width", 0.35}, {"reverb", 0.28}, {"reverb_decay", 0.8}}},
      {"knock", 0.0, nullptr, knock, {{"f0", 185.0}, {"width", 0.25}, {"reverb", 0.16}, {"reverb_decay", 0.5}}},
      {"footsteps", 0.0, nullptr, nullptr, {}},
      {"water_drop", 0.0, nullptr, waterDrop, {{"width", 0.3}, {"reverb", 0.18}, {"reverb_decay", 0.6}}},
      {"explosion", 0.0, nullptr, explosion, {{"f0", 60.0}, {"width", 0.7}, {"reverb", 0.18}, {"reverb_decay", 1.4}}},
      {"bird_chirp", 0.0, nullptr, nullptr, {}},
      {"crickets", 0.0, nullptr, nullptr, {}},
      {"wave_crash", 0.25, nullptr, nullptr, {}},
      {"wind_gust", 0.40, nullptr, nullptr, {}},
      {"splash", 0.0, nullptr, splash, {{"width", 0.4}, {"reverb", 0.14}, {"reverb_decay", 0.7}}},
      {"frog", 0.0, nullptr, nullptr, {}},
      {"owl", 0.0, nullptr, nullptr, {}},
      {"glass_break", 0.0, nullptr, glassBreak, {{"width", 0.6}, {"reverb", 0.2}, {"reverb_decay", 0.6}}},
      {"clock_tick", 0.0, nullptr, nullptr, {}},
      {"phone_ring", 0.0, nullptr, nullptr, {}},
      {"keyboard_typing", 0.0, nullptr, nullptr, {}},
      {"mouse_click", 0.0, nullptr, mouseClick, {{"width", 0.0}, {"reverb", 0.03}, {"reverb_decay", 0.25}}},
      {"camera_shutter", 0.0, nullptr, cameraShutter, {{"width", 0.1}, {"reverb", 0.05}, {"reverb_decay", 0.3}}},
      {"paper_rustle", 0.0, nullptr, nullptr, {}},
      {"cork_pop", 0.0, nullptr, corkPop, {{"width", 0.2}, {"reverb", 0.14}, {"reverb_decay", 0.5}}},
      {"coin_drop", 0.0, nullptr, coinDrop, {{"width", 0.2}, {"reverb", 0.16}, {"reverb_decay", 0.6}}},
      {"metal_clang", 0.0, nullptr, metalClang, {{"width", 0.4}, {"reverb", 0.2}, {"reverb_decay", 1.0}}},
      {"wood_creak", 0.5, nullptr, nullptr, {}},
      {"heartbeat", 0.0, nullptr, heartbeat, {{"width", 0.0}, {"reverb", 0.0}}},
      {"applause", 0.3, nullptr, nullptr, {}},
      {"pouring_water", 0.0, nullptr, nullptr, {}},
      {"can_opening", 0.0, nullptr, canOpening, {{"width", 0.1}, {"reverb", 0.08}, {"reverb_decay", 0.4}}},
      {"clock_alarm", 0.0, nullptr, nullptr, {}},
      {"punch", 0.0, nullptr, punch, {{"width", 0.2}, {"reverb", 0.1}, {"reverb_decay", 0.5}}},
      {"sword_clash", 0.0, nullptr, swordClash, {{"width", 0.4}, {"reverb", 0.18}, {"reverb_decay", 0.9}}},
      {"gunshot", 0.0, nullptr, gunshot, {{"width", 0.5}, {"reverb", 0.3}, {"reverb_decay", 1.2}}},
      {"car_horn", 0.0, nullptr, nullptr, {}},
      {"siren", 0.0, nullptr, nullptr, {}},
      {"engine", 0.0, nullptr, nullptr, {}},
      {"church_bell", 0.0, nullptr, churchBell, {{"width", 0.4}, {"reverb", 0.4}, {"reverb_decay", 2.0}}},
      {"fireworks", 0.3, nullptr, nullptr, {}},
   };
   return table;
}

void renderFoley(const Event &ev, Generator &rng, Params &p, int seed, bool tail, Vec &outL, Vec &outR) {
   const Role &role = roles()[static_cast<size_t>(ev.role)];
   const int64_t idx = ev.role;
   Generator crng(static_cast<uint64_t>((static_cast<int64_t>(seed) * 2654435761ll + idx * 40503 + 0x0F01E) & 0xFFFFFFFFll));
   Vec left, right;
   role.foley(ev, rng, crng, p, seed, left, right);
   left = finish(left, sr);
   right = finish(right, sr);
   const double peak = std::max({absMax(left), absMax(right), 1e-12});
   left = left / peak;
   right = right / peak;
   const double wet = get(p, "reverb", 0.0);
   if (wet > 0) {
      const double decay = get(p, "reverb_decay", 0.5);
      const int64_t s1 = rng.integers(0, int64_t(1) << 31);
      const int64_t s2 = rng.integers(0, int64_t(1) << 31);
      left = reverb(left, sr, decay, wet, static_cast<uint64_t>(s1), tail);
      right = reverb(right, sr, decay, wet, static_cast<uint64_t>(s2), tail);
   }
   left = foleyEdges(smoothLowpass(left, sr), sr);
   right = foleyEdges(smoothLowpass(right, sr), sr);
   const double width = clip(get(p, "width", 0.0), 0.0, 1.0) * (1.0 - std::fabs(ev.pan));
   const size_t n = left.size();
   Vec mid(n), side(n);
   for (size_t i = 0; i < n; ++i) {
      mid[i] = 0.5 * (left[i] + right[i]);
      side[i] = 0.5 * (left[i] - right[i]) * width * std::sqrt(0.5);
   }
   Vec pl, pr;
   pan(mid, ev.pan, pl, pr);
   const double gain = dbToGain(ev.gainDb);
   outL.resize(n);
   outR.resize(n);
   for (size_t i = 0; i < n; ++i) {
      outL[i] = (pl[i] + side[i]) * gain;
      outR[i] = (pr[i] - side[i]) * gain;
   }
}

} // namespace

const std::vector<std::string> &roleNames() {
   static const std::vector<std::string> names = [] {
      std::vector<std::string> v;
      for (const Role &r : roles())
         v.emplace_back(r.name);
      return v;
   }();
   return names;
}

int roleIndex(const std::string &name) {
   const auto &v = roleNames();
   const auto it = std::find(v.begin(), v.end(), name);
   return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

bool roleRendered(int role) {
   return role >= 0 && role < static_cast<int>(roles().size()) &&
          (roles()[static_cast<size_t>(role)].event || roles()[static_cast<size_t>(role)].foley);
}

void renderEvent(const Event &ev, int seed, bool tail, Vec &outL, Vec &outR) {
   const Role &role = roles()[static_cast<size_t>(ev.role)];
   const int64_t idx = ev.role;
   Generator rng(static_cast<uint64_t>(
      (static_cast<int64_t>(seed) * 1000003ll + idx * 7919 + toInt(ev.t * 1000)) & 0xFFFFFFFFll));
   Params p = role.params;
   if (role.foley) {
      renderFoley(ev, rng, p, seed, tail, outL, outR);
      return;
   }
   const auto prep = [&](Generator &g, bool align) {
      Vec m = role.event(ev, g, p);
      const double mu = mean(m);
      for (double &v : m)
         v = v - mu;
      if (align && continuousRole(role.name))
         m = alignPeak(m, sr, ev.apex - ev.t);
      return eventEdges(m, role.name);
   };
   Vec mono = prep(rng, true);
   const double wet = get(p, "reverb", 0.0);
   if (wet > 0)
      mono = wetOf(mono, p, wet, static_cast<uint64_t>(rng.integers(0, int64_t(1) << 31)), tail);
   const double width = clip(get(p, "width", 0.0), 0.0, 1.0);
   Vec left, right;
   if (width > 0) {
      Generator rng2(static_cast<uint64_t>(rng.integers(0, int64_t(1) << 31)));
      Vec mono2 = prep(rng2, false);
      if (wet > 0)
         mono2 = wetOf(mono2, p, wet, static_cast<uint64_t>(rng2.integers(0, int64_t(1) << 31)), tail);
      const size_t n = mono.size();
      left.resize(n);
      right.resize(n);
      for (size_t i = 0; i < n; ++i) {
         const double mid = 0.5 * (mono[i] + mono2[i]);
         const double side = 0.5 * (mono[i] - mono2[i]) * width;
         left[i] = mid + side;
         right[i] = mid - side;
      }
   } else {
      pan(mono, ev.pan, left, right);
   }
   const double gain = dbToGain(ev.gainDb);
   outL = left * gain;
   outR = right * gain;
}

void renderSpec(const Spec &spec, Vec &outL, Vec &outR) {
   const size_t n = static_cast<size_t>(toInt(spec.duration * sr));
   Vec evL(n, 0.0), evR(n, 0.0);
   int i = 0;
   for (const SpecEvent &se : spec.events) {
      const double dur = std::min(se.dur, spec.duration - se.t);
      if (dur <= 0.02 || !roleRendered(se.role)) {
         ++i;
         continue;
      }
      Event ev;
      ev.t = se.t;
      ev.role = se.role;
      ev.dur = dur;
      ev.apex = se.t + roles()[static_cast<size_t>(se.role)].apex * dur;
      ev.gainDb = se.gainDb;
      ev.pan = se.pan;
      ev.seed = se.seed;
      const int seed = ev.seed ? ev.seed : 200 + i;
      Vec l, r;
      renderEvent(ev, seed, true, l, r);
      const int64_t start = std::max<int64_t>(0, static_cast<int64_t>(roundEven(ev.t * sr)));
      for (size_t k = 0; k < l.size() && static_cast<size_t>(start) + k < n; ++k) {
         evL[static_cast<size_t>(start) + k] += l[k];
         evR[static_cast<size_t>(start) + k] += r[k];
      }
      ++i;
   }
   const size_t b = std::min<size_t>(n, static_cast<size_t>(toInt(0.01 * sr)));
   if (b > 1) {
      const Vec ramp = linspace(1.0, 0.0, b);
      for (size_t k = 0; k < b; ++k) {
         evL[n - b + k] *= ramp[k];
         evR[n - b + k] *= ramp[k];
      }
   }
   const double peak = std::max(absMax(evL), absMax(evR));
   const double g = peak > 1e-6 ? 0.89 / peak : 0.0;
   outL = evL * g;
   outR = evR * g;
}

} // namespace jkit::orb
