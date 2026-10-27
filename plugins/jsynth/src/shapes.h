#pragma once

// The shape library for an envelope in Shape mode, after XAOC's Zadar: each shape is one drawn
// contour, 0..1 in level over 0..1 of its cycle, that the envelope stretches over its Time and can
// warp, bend, reverse and repeat. They are worked out once, as tables, so the engine reads them and
// the panel draws them from the same numbers.
//
// Shapes are grouped in banks of six. A shape's place in the list is its param value, so a new
// one only ever goes at the end.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace jsynth {

// Points per shape, across one cycle; the table has one more, the cycle's end.
constexpr int kShapePoints = 512;

struct ShapeBank {
   const char *name;
   const char *shapes[6];
};

inline const std::vector<ShapeBank> &shapeBanks() {
   static const std::vector<ShapeBank> banks = {
      {"Rise", {"Ramp Up", "Bloom", "Lift", "Slow S", "Late Swell", "Stair Up"}},
      {"Fall", {"Ramp Down", "Pluck", "Fade", "Long Tail", "Drop", "Stair Down"}},
      {"Swell", {"Arch", "Breath", "Bow", "Plateau", "Twin Hills", "Surge"}},
      {"Pulse", {"Gate 4", "Gate 8", "Stutter", "Bounce", "Heartbeat", "Echoes"}},
      {"Waves", {"Wobble 4", "Wobble 8", "Speed Up", "Slow Down", "Rising Wobble", "Fading Wobble"}},
      {"Steps", {"Arp Up", "Arp Down", "Up Down", "Random 8", "Random 16", "Sparse Steps"}},
      {"Drift", {"Wander", "Brownian", "Flicker", "Gusts", "Sparkle", "Shimmer"}},
      {"Strike", {"Comet", "Bell", "Saw Train", "Sawtooth Rise", "Collapse", "Tremble"}},
      {"ADSR", {"ADSR", "Punch", "Slow Pad", "ASR", "AHD", "Organ"}},
      {"Acoustic", {"Piano", "Brass", "Strings", "Guitar", "Double Attack", "Swell and Hit"}},
   };
   return banks;
}

constexpr int kShapeBankSize = 6;
constexpr int kShapeCount = 10 * kShapeBankSize;

// "Bank: Name", the param's choice names.
inline const std::vector<std::string> &shapeNames() {
   static const std::vector<std::string> names = [] {
      std::vector<std::string> n;
      for (const ShapeBank &b : shapeBanks())
         for (const char *s : b.shapes)
            n.push_back(std::string(b.name) + ": " + s);
      return n;
   }();
   return names;
}

// A shape's index by its name in the bank ("Breath"), or 0 when there's none.
inline int shapeIndex(const std::string &name) {
   int i = 0;
   for (const ShapeBank &b : shapeBanks())
      for (const char *s : b.shapes) {
         if (name == s)
            return i;
         ++i;
      }
   return 0;
}

namespace detail {

constexpr double kPi = 3.14159265358979323846;

inline double smooth(double x) {
   x = std::clamp(x, 0.0, 1.0);
   return x * x * (3.0 - 2.0 * x);
}
// The same random number for the same seed and index, on every build.
inline double hash(uint32_t seed, uint32_t i) {
   uint32_t h = seed * 0x9e3779b9u ^ (i + 0x7f4a7c15u) * 0x85ebca6bu;
   h ^= h >> 15;
   h *= 0x2c1b3c6du;
   h ^= h >> 12;
   h *= 0x297a2d39u;
   h ^= h >> 15;
   return (h & 0xffffff) / 16777215.0;
}
// Smooth value noise: random points `count` to a cycle, eased between, and the cycle's end back on
// its start.
inline double valueNoise(uint32_t seed, double x, int count) {
   const double p = x * count;
   const int i = std::min(static_cast<int>(std::floor(p)), count - 1);
   const double a = hash(seed, i), b = hash(seed, (i + 1) % count);
   return a + (b - a) * smooth(p - i);
}
// Steps of `count` levels, each held, with a short slope so a step never clicks.
inline double steps(double x, int count, const double *levels) {
   const double p = std::min(x * count, count - 1e-9);
   const int i = static_cast<int>(p);
   const double f = p - i, edge = 0.04;
   const double prev = levels[i > 0 ? i - 1 : 0];
   return f < edge ? prev + (levels[i] - prev) * smooth(f / edge) : levels[i];
}
// A pulse of `width` (of each of `count` beats), with soft edges.
inline double gates(double x, int count, double width) {
   const double f = x * count - std::floor(x * count), edge = 0.03;
   if (f < edge)
      return smooth(f / edge);
   if (f < width)
      return 1.0;
   if (f < width + edge)
      return 1.0 - smooth((f - width) / edge);
   return 0.0;
}
// A quick rise and an exponential fall, starting at `at` and `length` long.
inline double hit(double x, double at, double length, double sharp) {
   if (x < at)
      return 0.0;
   const double t = (x - at) / length;
   const double rise = 0.02 / length;
   return t < rise ? smooth(t / rise) : std::exp(-sharp * (t - rise));
}

// Segments through points, as an ADSR draws them: from (0, 0) to each {x, y} in turn, each bowed
// by its curve (+ fast then slow, as an analog stage; 0 straight; - slow then fast).
struct StagePoint {
   double x, y, curve;
};
inline double stages(double x, std::initializer_list<StagePoint> points) {
   double x0 = 0.0, y0 = 0.0;
   for (const StagePoint &p : points) {
      if (x <= p.x) {
         const double t = p.x > x0 ? (x - x0) / (p.x - x0) : 1.0;
         const double k = std::fabs(p.curve);
         double f = t;
         if (k > 1e-6)
            f = p.curve > 0 ? (1.0 - std::exp(-k * t)) / (1.0 - std::exp(-k)) : (std::exp(k * t) - 1.0) / (std::exp(k) - 1.0);
         return y0 + (p.y - y0) * f;
      }
      x0 = p.x, y0 = p.y;
   }
   return y0;
}

inline double shapeAt(int index, double x) {
   using std::exp, std::sin, std::cos, std::pow, std::fabs;
   const double tau = 2.0 * kPi;
   switch (index) {
   // Rise
   case 0: return x;
   case 1: return pow(x, 3.0);
   case 2: return 1.0 - pow(1.0 - x, 3.0);
   case 3: return smooth(smooth(x));
   case 4: return x < 0.45 ? 0.0 : smooth((x - 0.45) / 0.55);
   case 5: {
      static const double l[8] = {0.0, 1 / 7.0, 2 / 7.0, 3 / 7.0, 4 / 7.0, 5 / 7.0, 6 / 7.0, 1.0};
      return steps(x, 8, l);
   }
   // Fall
   case 6: return 1.0 - x;
   case 7: return x < 0.01 ? smooth(x / 0.01) : (exp(-7.0 * (x - 0.01)) - exp(-7.0)) / (1.0 - exp(-7.0));
   case 8: return 1.0 - pow(x, 3.0);
   case 9: return pow(1.0 - x, 4.0);
   case 10: return x < 0.7 ? 1.0 : 1.0 - smooth((x - 0.7) / 0.1);
   case 11: {
      static const double l[8] = {1.0, 6 / 7.0, 5 / 7.0, 4 / 7.0, 3 / 7.0, 2 / 7.0, 1 / 7.0, 0.0};
      return steps(x, 8, l);
   }
   // Swell
   case 12: return sin(kPi * x);
   case 13: return x < 0.7 ? smooth(x / 0.7) : 1.0 - smooth((x - 0.7) / 0.3);
   case 14: return x < 0.15 ? smooth(x / 0.15) : pow(1.0 - (x - 0.15) / 0.85, 2.0);
   case 15: return x < 0.2 ? smooth(x / 0.2) : x < 0.8 ? 1.0 : 1.0 - smooth((x - 0.8) / 0.2);
   case 16: return x < 0.5 ? sin(tau * x) : 0.6 * sin(tau * (x - 0.5));
   case 17: {
      // Rises a little, sinks back, then rises all the way and falls away.
      const double a = 0.45 * sin(kPi * std::min(x / 0.35, 1.0));
      const double b = x < 0.35 ? 0.0 : sin(kPi * (x - 0.35) / 0.65);
      return std::max(a, b);
   }
   // Pulse
   case 18: return gates(x, 4, 0.5);
   case 19: return gates(x, 8, 0.4);
   case 20: {
      // Gates that come faster and faster: each beat 0.8 of the last.
      const double r = 0.8, n = 12, total = (1.0 - pow(r, n)) / (1.0 - r);
      const double p = x * total * (1.0 - r);
      const double k = std::log(1.0 - p) / std::log(r); // which beat, fractionally
      return gates(k / n, static_cast<int>(n), 0.5);
   }
   case 21: {
      // A dropped ball: bounces that halve in height and shorten.
      double at = 0.0, len = 0.34, h = 1.0;
      for (int i = 0; i < 8; ++i) {
         if (x < at + len) {
            const double t = (x - at) / len;
            return h * 4.0 * t * (1.0 - t);
         }
         at += len;
         len *= 0.7;
         h *= 0.5;
      }
      return 0.0;
   }
   case 22: return std::max(hit(x, 0.0, 0.3, 9.0), 0.7 * hit(x, 0.18, 0.3, 9.0)) * (x < 0.97 ? 1.0 : (1.0 - x) / 0.03);
   case 23: {
      double y = 0.0;
      for (int i = 0; i < 6; ++i)
         y = std::max(y, pow(0.62, i) * hit(x, i / 6.0, 1.0 / 6.0, 10.0));
      return y;
   }
   // Waves
   case 24: return 0.5 - 0.5 * cos(tau * 4.0 * x);
   case 25: return 0.5 - 0.5 * cos(tau * 8.0 * x);
   case 26: return 0.5 - 0.5 * cos(tau * (2.0 * x + 6.0 * x * x)); // 2 cycles a cycle rising to 14
   case 27: return 0.5 - 0.5 * cos(tau * (14.0 * x - 6.0 * x * x));
   case 28: return x * (0.5 - 0.5 * cos(tau * 6.0 * x));
   case 29: return (1.0 - x) * (0.5 - 0.5 * cos(tau * 6.0 * x + kPi)) * smooth(x / 0.05);
   // Steps
   case 30: {
      static const double l[4] = {0.25, 0.5, 0.75, 1.0};
      return steps(x, 4, l);
   }
   case 31: {
      static const double l[4] = {1.0, 0.75, 0.5, 0.25};
      return steps(x, 4, l);
   }
   case 32: {
      static const double l[6] = {0.25, 0.5, 0.75, 1.0, 0.75, 0.5};
      return steps(x, 6, l);
   }
   case 33: {
      double l[8];
      for (int i = 0; i < 8; ++i)
         l[i] = 0.1 + 0.9 * hash(33, i);
      return steps(x, 8, l);
   }
   case 34: {
      double l[16];
      for (int i = 0; i < 16; ++i)
         l[i] = hash(34, i);
      return steps(x, 16, l);
   }
   case 35: {
      // Mostly silence, and a few held steps.
      double l[12];
      for (int i = 0; i < 12; ++i)
         l[i] = hash(35, i) < 0.4 ? 0.3 + 0.7 * hash(135, i) : 0.0;
      return steps(x, 12, l);
   }
   // Drift: random, but the same every time, and back where it started at the cycle's end so a
   // loop never jumps.
   case 36: {
      const double y = 0.6 * valueNoise(36, x, 6) + 0.4 * valueNoise(136, x, 13);
      return 0.15 + 0.85 * y;
   }
   case 37: {
      // A random walk on 1/f noise: big slow moves with smaller ones on top.
      const double y = 0.5 * valueNoise(37, x, 4) + 0.3 * valueNoise(137, x, 9) + 0.2 * valueNoise(237, x, 23);
      return std::clamp((y - 0.2) / 0.6, 0.0, 1.0);
   }
   case 38: return std::clamp(0.55 + 0.45 * (valueNoise(38, x, 48) - 0.5) * 2.0 * valueNoise(138, x, 5), 0.0, 1.0);
   case 39: {
      const double y = sin(tau * x) * 0.3 + sin(tau * 3.0 * x + 1.3) * 0.25 + sin(tau * 7.0 * x + 0.4) * 0.12;
      return std::clamp(0.5 + y, 0.0, 1.0);
   }
   case 40: {
      double y = 0.0;
      for (int i = 0; i < 14; ++i)
         y = std::max(y, (0.3 + 0.7 * hash(40, i)) * hit(x, hash(140, i) * 0.92, 0.08, 12.0));
      return y;
   }
   case 41: return sin(kPi * x) * (0.75 + 0.25 * (valueNoise(41, x, 40) * 2.0 - 1.0));
   // Strike
   case 42: return x < 0.01 ? smooth(x / 0.01) : 0.3 * exp(-30.0 * (x - 0.01)) + 0.7 * pow(1.0 - (x - 0.01) / 0.99, 1.5);
   case 43: return x < 0.005 ? smooth(x / 0.005) : exp(-5.0 * x) * (0.8 + 0.2 * cos(tau * 9.0 * x));
   case 44: {
      const double f = x * 4.0 - std::floor(x * 4.0);
      const double h = 1.0 - 0.2 * std::floor(x * 4.0);
      return h * (f < 0.02 ? smooth(f / 0.02) : 1.0 - (f - 0.02) / 0.98);
   }
   case 45: {
      // A ramp up that sags back a little five times on the way.
      const double f = x * 5.0 - std::floor(x * 5.0);
      return f < 0.85 ? x : x * (1.0 - 0.4 * sin(kPi * (f - 0.85) / 0.15));
   }
   case 46: {
      static const double l[6] = {1.0, 0.8, 0.82, 0.5, 0.52, 0.0};
      return x < 0.08 ? smooth(x / 0.08) : steps((x - 0.08) / 0.92, 6, l);
   }
   case 47: return sin(kPi * x) * (0.7 + 0.3 * sin(tau * 23.0 * x));
   // ADSR: the classic stages drawn, each with a sustain to set the sustain point in.
   case 48: return stages(x, {{0.08, 1.0, 2.0}, {0.25, 0.6, 4.0}, {0.75, 0.6, 0.0}, {1.0, 0.0, 4.0}});
   case 49: return stages(x, {{0.015, 1.0, 1.0}, {0.15, 0.45, 5.0}, {0.8, 0.45, 0.0}, {1.0, 0.0, 4.0}});
   case 50: return stages(x, {{0.35, 1.0, -1.5}, {0.45, 0.85, 2.0}, {0.7, 0.85, 0.0}, {1.0, 0.0, 2.5}});
   case 51: return stages(x, {{0.15, 1.0, 2.0}, {0.8, 1.0, 0.0}, {1.0, 0.0, 4.0}});
   case 52: return stages(x, {{0.03, 1.0, 1.0}, {0.35, 1.0, 0.0}, {1.0, 0.0, 5.0}});
   case 53: return stages(x, {{0.01, 1.0, 0.0}, {0.95, 1.0, 0.0}, {1.0, 0.0, 1.0}});
   // Acoustic: envelopes after instruments.
   case 54: return stages(x, {{0.005, 1.0, 0.0}, {0.1, 0.5, 4.0}, {0.85, 0.15, 3.0}, {1.0, 0.0, 3.0}});
   case 55: return stages(x, {{0.07, 1.0, 1.5}, {0.2, 0.72, 3.0}, {0.8, 0.8, -0.5}, {1.0, 0.0, 4.0}});
   case 56: return stages(x, {{0.25, 0.9, -1.0}, {0.78, 0.92, 0.0}, {1.0, 0.0, 2.0}});
   case 57: return stages(x, {{0.004, 1.0, 0.0}, {0.45, 0.3, 4.0}, {0.85, 0.18, 1.0}, {1.0, 0.0, 3.0}});
   case 58: return stages(x, {{0.015, 1.0, 1.0}, {0.1, 0.4, 3.0}, {0.18, 0.9, 1.5}, {0.35, 0.6, 3.0}, {0.8, 0.6, 0.0},
                              {1.0, 0.0, 4.0}});
   case 59: return stages(x, {{0.6, 0.55, -2.5}, {0.62, 1.0, 0.0}, {1.0, 0.0, 5.0}});
   default: return 0.0;
   }
}

} // namespace detail

// Every shape, as a table of kShapePoints + 1 levels.
inline const float *shapeTable(int index) {
   using Table = std::array<float, kShapePoints + 1>;
   static const std::vector<Table> tables = [] {
      std::vector<Table> t(kShapeCount);
      for (int s = 0; s < kShapeCount; ++s)
         for (int i = 0; i <= kShapePoints; ++i)
            t[s][i] = static_cast<float>(std::clamp(detail::shapeAt(s, static_cast<double>(i) / kShapePoints), 0.0, 1.0));
      return t;
   }();
   return tables[std::clamp(index, 0, kShapeCount - 1)].data();
}

} // namespace jsynth
