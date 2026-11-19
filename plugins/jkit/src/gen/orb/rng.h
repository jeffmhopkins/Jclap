#pragma once

// numpy.random.default_rng(seed), number for number: SeedSequence seeding, the PCG64 bit generator,
// and the distributions ORB's renderer draws from (uniform, normal, exponential, gamma, Pareto,
// Poisson, bounded integers, choice). ORB's sounds are made from these draws, so drawing the same
// numbers makes the same sounds as the reference renderer, and lets the tests compare the two
// sample by sample.
//
// The algorithms are numpy's (numpy/random/src, BSD-3-Clause, Copyright (c) 2005-2024 NumPy
// Developers; PCG64 by Melissa O'Neill, Apache-2.0/MIT).

#include <cmath>
#include <cstdint>
#include <vector>

#include "gen/orb/ziggurat.h"

namespace jkit::orb {

// A 128-bit unsigned number, for PCG64's state (MSVC has no __int128).
struct U128 {
   uint64_t hi = 0, lo = 0;
};

inline U128 add(U128 a, U128 b) {
   U128 r;
   r.lo = a.lo + b.lo;
   r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
   return r;
}

inline void mul64(uint64_t a, uint64_t b, uint64_t &hi, uint64_t &lo) {
   const uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
   const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
   const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
   lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
   hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

inline U128 mul(U128 a, U128 b) {
   U128 r;
   mul64(a.lo, b.lo, r.hi, r.lo);
   r.hi += a.lo * b.hi + a.hi * b.lo;
   return r;
}

class Generator {
public:
   // default_rng(seed) for a seed below 2^64.
   explicit Generator(uint64_t seed) {
      // SeedSequence(seed).generate_state(4, uint64)
      uint32_t entropy[2] = {static_cast<uint32_t>(seed), static_cast<uint32_t>(seed >> 32)};
      const int words = seed >> 32 ? 2 : 1;
      uint32_t pool[4];
      uint32_t hashConst = 0x43b0d7e5u;
      const auto hashmix = [&](uint32_t value) {
         value ^= hashConst;
         hashConst *= 0x931e8875u;
         value *= hashConst;
         value ^= value >> 16;
         return value;
      };
      const auto mix = [](uint32_t x, uint32_t y) {
         uint32_t r = 0xca01f9ddu * x - 0x4973f715u * y;
         r ^= r >> 16;
         return r;
      };
      for (int i = 0; i < 4; ++i)
         pool[i] = hashmix(i < words ? entropy[i] : 0u);
      for (int s = 0; s < 4; ++s)
         for (int d = 0; d < 4; ++d)
            if (s != d)
               pool[d] = mix(pool[d], hashmix(pool[s]));
      uint32_t state32[8];
      uint32_t hb = 0x8b51f9ddu;
      for (int i = 0; i < 8; ++i) {
         uint32_t v = pool[i % 4];
         v ^= hb;
         hb *= 0x58f38dedu;
         v *= hb;
         v ^= v >> 16;
         state32[i] = v;
      }
      uint64_t v[4];
      for (int i = 0; i < 4; ++i)
         v[i] = static_cast<uint64_t>(state32[2 * i]) | static_cast<uint64_t>(state32[2 * i + 1]) << 32;
      // pcg64_set_seed: state = (v0, v1), inc = (v2, v3)
      const U128 initState{v[0], v[1]}, initSeq{v[2], v[3]};
      state_ = U128{};
      inc_ = U128{(initSeq.hi << 1) | (initSeq.lo >> 63), (initSeq.lo << 1) | 1u};
      step();
      state_ = add(state_, initState);
      step();
   }

   uint64_t next64() {
      step();
      const uint64_t x = state_.hi ^ state_.lo;
      const unsigned rot = static_cast<unsigned>(state_.hi >> 58);
      return (x >> rot) | (x << ((64 - rot) & 63));
   }
   uint32_t next32() {
      if (hasU32_) {
         hasU32_ = false;
         return u32_;
      }
      const uint64_t n = next64();
      hasU32_ = true;
      u32_ = static_cast<uint32_t>(n >> 32);
      return static_cast<uint32_t>(n & 0xFFFFFFFFu);
   }
   double nextDouble() { return static_cast<double>(next64() >> 11) * (1.0 / 9007199254740992.0); }

   // ---- distributions ----

   double random() { return nextDouble(); }
   double uniform(double lo, double hi) { return lo + (hi - lo) * nextDouble(); }
   std::vector<double> uniform(double lo, double hi, size_t n) {
      std::vector<double> out(n);
      for (double &x : out)
         x = uniform(lo, hi);
      return out;
   }

   double normal() {
      for (;;) {
         uint64_t r = next64();
         const int idx = static_cast<int>(r & 0xff);
         r >>= 8;
         const int sign = static_cast<int>(r & 0x1);
         const uint64_t rabs = (r >> 1) & 0x000fffffffffffffull;
         double x = static_cast<double>(rabs) * zig::wi[idx];
         if (sign & 0x1)
            x = -x;
         if (rabs < zig::ki[idx])
            return x;
         if (idx == 0) {
            for (;;) {
               const double xx = -zig::kNorInvR * std::log1p(-nextDouble());
               const double yy = -std::log1p(-nextDouble());
               if (yy + yy > xx * xx)
                  return ((rabs >> 8) & 0x1) ? -(zig::kNorR + xx) : zig::kNorR + xx;
            }
         }
         if (((zig::fi[idx - 1] - zig::fi[idx]) * nextDouble() + zig::fi[idx]) < std::exp(-0.5 * x * x))
            return x;
      }
   }
   std::vector<double> normal(size_t n) {
      std::vector<double> out(n);
      for (double &x : out)
         x = normal();
      return out;
   }

   double exponential() {
      for (;;) {
         uint64_t ri = next64();
         ri >>= 3;
         const int idx = static_cast<int>(ri & 0xFF);
         ri >>= 8;
         const double x = static_cast<double>(ri) * zig::we[idx];
         if (ri < zig::ke[idx])
            return x;
         if (idx == 0)
            return zig::kExpR - std::log1p(-nextDouble());
         if ((zig::fe[idx - 1] - zig::fe[idx]) * nextDouble() + zig::fe[idx] < std::exp(-x))
            return x;
      }
   }
   double exponential(double scale) { return scale * exponential(); }

   double gamma(double shape, double scale) { return scale * standardGamma(shape); }
   double standardGamma(double shape) {
      if (shape == 1.0)
         return exponential();
      if (shape == 0.0)
         return 0.0;
      if (shape < 1.0) {
         for (;;) {
            const double U = nextDouble();
            const double V = exponential();
            if (U <= 1.0 - shape) {
               const double X = std::pow(U, 1.0 / shape);
               if (X <= V)
                  return X;
            } else {
               const double Y = -std::log((1 - U) / shape);
               const double X = std::pow(1.0 - shape + shape * Y, 1.0 / shape);
               if (X <= (V + Y))
                  return X;
            }
         }
      }
      const double b = shape - 1.0 / 3.0;
      const double c = 1.0 / std::sqrt(9 * b);
      for (;;) {
         double X, V;
         do {
            X = normal();
            V = 1.0 + c * X;
         } while (V <= 0.0);
         V = V * V * V;
         const double U = nextDouble();
         if (U < 1.0 - 0.0331 * (X * X) * (X * X))
            return b * V;
         if (std::log(U) < 0.5 * X * X + b * (1.0 - V + std::log(V)))
            return b * V;
      }
   }

   double pareto(double a) { return std::expm1(exponential() / a); }
   double lognormal(double mean, double sigma) { return std::exp(mean + sigma * normal()); }

   int64_t poisson(double lam) {
      if (lam >= 10)
         return poissonPtrs(lam);
      if (lam == 0)
         return 0;
      const double enlam = std::exp(-lam);
      int64_t X = 0;
      double prod = 1.0;
      for (;;) {
         prod *= nextDouble();
         if (prod > enlam)
            X += 1;
         else
            return X;
      }
   }

   // integers(lo, hi) (hi exclusive), one or `n` of them.
   int64_t integers(int64_t lo, int64_t hi) {
      int64_t out;
      integersFill(lo, hi, &out, 1);
      return out;
   }
   std::vector<int64_t> integers(int64_t lo, int64_t hi, size_t n) {
      std::vector<int64_t> out(n);
      integersFill(lo, hi, out.data(), n);
      return out;
   }

   // choice(values, size) with replacement and no weights.
   std::vector<double> choice(const std::vector<double> &values, size_t n) {
      const std::vector<int64_t> idx = integers(0, static_cast<int64_t>(values.size()), n);
      std::vector<double> out(n);
      for (size_t i = 0; i < n; ++i)
         out[i] = values[static_cast<size_t>(idx[i])];
      return out;
   }
   // choice(len(p), size, p=p): indices drawn with weights p (which sum to 1).
   std::vector<int64_t> choiceWeighted(const std::vector<double> &p, size_t n) {
      std::vector<double> cdf(p.size());
      double acc = 0.0;
      for (size_t i = 0; i < p.size(); ++i)
         cdf[i] = acc += p[i];
      const double last = cdf.empty() ? 1.0 : cdf.back();
      for (double &c : cdf)
         c /= last;
      std::vector<int64_t> out(n);
      for (size_t i = 0; i < n; ++i) {
         const double u = nextDouble();
         // searchsorted(side='right'): the first index whose cdf is > u
         size_t a = 0, b = cdf.size();
         while (a < b) {
            const size_t m = (a + b) / 2;
            if (cdf[m] <= u)
               a = m + 1;
            else
               b = m;
         }
         out[i] = static_cast<int64_t>(a);
      }
      return out;
   }

private:
   void step() {
      static const U128 kMult{2549297995355413924ull, 4865540595714422341ull};
      state_ = add(mul(state_, kMult), inc_);
   }

   // Ranges under 2^32 draw 32-bit halves through the generator's own buffer (next32).
   void integersFill(int64_t lo, int64_t hi, int64_t *out, size_t n) {
      const uint64_t rng = static_cast<uint64_t>(hi - 1 - lo);
      if (rng == 0) {
         for (size_t i = 0; i < n; ++i)
            out[i] = lo;
         return;
      }
      if (rng <= 0xFFFFFFFFull) {
         if (rng == 0xFFFFFFFFull) {
            for (size_t i = 0; i < n; ++i)
               out[i] = lo + static_cast<int64_t>(next32());
            return;
         }
         const uint32_t r32 = static_cast<uint32_t>(rng);
         const uint32_t excl = r32 + 1;
         for (size_t i = 0; i < n; ++i) {
            uint64_t m = static_cast<uint64_t>(next32()) * excl;
            uint32_t leftover = static_cast<uint32_t>(m & 0xFFFFFFFFu);
            if (leftover < excl) {
               const uint32_t threshold = (0xFFFFFFFFu - r32) % excl;
               while (leftover < threshold) {
                  m = static_cast<uint64_t>(next32()) * excl;
                  leftover = static_cast<uint32_t>(m & 0xFFFFFFFFu);
               }
            }
            out[i] = lo + static_cast<int64_t>(m >> 32);
         }
         return;
      }
      // 64-bit Lemire (ORB never asks for a range this wide; kept for completeness).
      const uint64_t excl = rng + 1;
      for (size_t i = 0; i < n; ++i) {
         uint64_t hi64, lo64;
         mul64(next64(), excl, hi64, lo64);
         if (lo64 < excl) {
            const uint64_t threshold = (0xFFFFFFFFFFFFFFFFull - rng) % excl;
            while (lo64 < threshold)
               mul64(next64(), excl, hi64, lo64);
         }
         out[i] = lo + static_cast<int64_t>(hi64);
      }
   }

   static double loggam(double x) {
      static const double a[10] = {8.333333333333333e-02, -2.777777777777778e-03, 7.936507936507937e-04,
                                   -5.952380952380952e-04, 8.417508417508418e-04, -1.917526917526918e-03,
                                   6.410256410256410e-03, -2.955065359477124e-02, 1.796443723688307e-01,
                                   -1.39243221690590e+00};
      if (x == 1.0 || x == 2.0)
         return 0.0;
      const int64_t n = x < 7.0 ? static_cast<int64_t>(7 - x) : 0;
      double x0 = x + static_cast<double>(n);
      const double x2 = (1.0 / x0) * (1.0 / x0);
      const double lg2pi = 1.8378770664093453e+00;
      double gl0 = a[9];
      for (int k = 8; k >= 0; --k) {
         gl0 *= x2;
         gl0 += a[k];
      }
      double gl = gl0 / x0 + 0.5 * lg2pi + (x0 - 0.5) * std::log(x0) - x0;
      if (x < 7.0)
         for (int64_t k = 1; k <= n; ++k) {
            gl -= std::log(x0 - 1.0);
            x0 -= 1.0;
         }
      return gl;
   }

   int64_t poissonPtrs(double lam) {
      const double slam = std::sqrt(lam);
      const double loglam = std::log(lam);
      const double b = 0.931 + 2.53 * slam;
      const double a = -0.059 + 0.02483 * b;
      const double invalpha = 1.1239 + 1.1328 / (b - 3.4);
      const double vr = 0.9277 - 3.6224 / (b - 2);
      for (;;) {
         const double U = nextDouble() - 0.5;
         const double V = nextDouble();
         const double us = 0.5 - std::fabs(U);
         const int64_t k = static_cast<int64_t>(std::floor((2 * a / us + b) * U + lam + 0.43));
         if ((us >= 0.07) && (V <= vr))
            return k;
         if ((k < 0) || ((us < 0.013) && (V > us)))
            continue;
         if ((std::log(V) + std::log(invalpha) - std::log(a / (us * us) + b)) <=
             (-lam + static_cast<double>(k) * loglam - loggam(static_cast<double>(k + 1))))
            return k;
      }
   }

   U128 state_, inc_;
   bool hasU32_ = false;
   uint32_t u32_ = 0;
};

} // namespace jkit::orb
