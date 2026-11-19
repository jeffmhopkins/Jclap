#pragma once

// The pieces of NumPy that ORB's renderer is written in, and its DSP helpers
// (orb/render/dsp.py and the shared helpers of foley.py and natural.py), in C++.
//
// Each function follows its Python original step for step, in the same order of operations, so the
// port's output matches the reference renderer to rounding. The comments name the original.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gen/orb/rng.h"

namespace jkit::orb {

using Vec = std::vector<double>;
using CVec = std::vector<std::complex<double>>;

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2.0 * kPi;
constexpr double kLn1000 = 6.907755278982137;
constexpr int kSr = 48000; // ORB renders at 48 kHz

// ---- numpy ----

// (n - 1).bit_length() as a shift: the smallest power of two >= n.
size_t nextPow2(size_t n);
CVec rfft(const Vec &x, size_t n);
CVec rfft(const Vec &x);
Vec irfft(const CVec &X, size_t n);
Vec rfftfreq(size_t n, double sr);
Vec linspace(double a, double b, size_t n);
// np.arange(n) / sr
Vec times(size_t n, double sr);
Vec hanning(size_t m);
Vec cumsum(const Vec &x);
// np.interp(x, xp, fp), with optional left value (defaults to fp[0]).
Vec interp(const Vec &x, const Vec &xp, const Vec &fp);
double interp1(double x, const Vec &xp, const Vec &fp);
double sum(const Vec &x);
double mean(const Vec &x);
double absMax(const Vec &x);
size_t argmax(const Vec &x);
Vec zeros(size_t n);
// x[lo:hi] (clamped)
Vec slice(const Vec &x, size_t lo, size_t hi);
// np.pad(x, (a, b))
Vec padZeros(const Vec &x, size_t a, size_t b);
inline int64_t toInt(double x) { return static_cast<int64_t>(x); } // Python int(): toward zero
double clip(double x, double lo, double hi);
// Python's round(): to nearest, ties to even.
double roundEven(double x);

// ---- dsp.py ----

double dbToGain(double db);
Vec fade(Vec x, int sr, double fadeIn = 0.003, double fadeOut = 0.01);
void pan(const Vec &x, double p, Vec &left, Vec &right);
void stft(const Vec &x, int nFft, int hop, std::vector<CVec> &frames);
Vec istft(const std::vector<CVec> &X, int nFft, int hop, size_t length);
Vec bandpassTimevarying(const Vec &x, const Vec &centers, const Vec &widths, int sr, int nFft = 1024, int hop = 256);
// lo / hi < 0 means None.
Vec bandlimit(const Vec &x, int sr, double lo, double hi, int order = 4, size_t pad = 2048);
Vec shapedBurst(Generator &rng, size_t n, int64_t k, double decay, int sr, double lo = 2000.0, double hi = 12000.0);
Vec modal(double f0, double dur, int sr, const Vec &ratios, const Vec &t60, const Vec &gains);
Vec sweepSine(double f0, double f1, double dur, int sr, double curve = 1.0);
Vec envApex(size_t n, int sr, double apexS, double risePower = 1.6, double fallPower = 3.2);
Vec saturate(const Vec &x, double drive = 1.5);
Vec alignPeak(const Vec &x, int sr, double targetS, double maxShift = 0.05);
Vec reverb(const Vec &x, int sr, double decay, double wet, uint64_t seed, bool tail);

// ---- foley.py helpers ----

Vec fftconv(const Vec &a, const Vec &b, size_t nOut);
Vec modesIr(const Vec &freqs, const Vec &t60s, const Vec &gains, int sr, size_t nMax);
Vec ring(const Vec &exc, const Vec &freqs, const Vec &t60s, const Vec &gains, int sr);
Vec burst(Generator &rng, size_t n, int64_t at, int64_t length, double tau, int sr, double rise = 0.001);
Vec pulse(size_t n, int64_t at, int64_t width);
Vec smooth(Generator &rng, size_t n, int sr, double rate);
Vec fadeOutEnv(size_t n, int sr, double length);
Vec norm(const Vec &x);
Vec attack(size_t n, int sr, double at, double rise);
Vec smoothLowpass(const Vec &x, int sr, double fPass = 9000.0, double fStop = 12000.0, size_t pad = 2048);
Vec foleyEdges(Vec x, int sr);
Vec finish(const Vec &x, int sr);

// ---- natural.py helpers ----

Vec strike(size_t n, int sr, int64_t at, const Vec &freqs, const Vec &t60s, const Vec &gains, Generator &rng,
           double contact = 0.0008, double noise = 0.0);
Vec grains(Generator &rng, size_t n, const Vec &times, const Vec &amps, int sr);
Vec decorrelate(const Vec &x, int sr, Generator &crng, int taps = 6, double span = 0.012);
size_t waterFast(size_t m);
Vec waterBand(const Vec &x, int sr, double lo, double hi, int order = 2, size_t pad = 2048);
Vec waterNoise(Generator &rng, size_t n, int sr, double lo, double hi, int order = 2);
Vec waterBubbles(size_t n, int sr, const Vec &times, const Vec &f0s, const Vec &amps, double damp = 1.0,
                 double rise = 0.1, int dec = 2, double floor = 5.5);
Vec waterTimes(Generator &crng, const Vec &rate, int sr, int64_t cap = 200000);
Vec waterCrackle(Generator &rng, Generator &crng, size_t n, int sr, const Vec &rate, double lo, double hi,
                 double alpha = 1.6, int order = 2);
Vec waterRadii(Generator &crng, size_t k, double rLo, double rHi, double slope = 2.5);
void bellsPlace(Vec &out, int64_t at, const Vec &x, double amp = 1.0);
Vec bellsMode(double f, double t60, int sr, size_t k, size_t block = 2048);
// length < 0: None
Vec bellsModes(const Vec &freqs, const Vec &t60s, const Vec &gains, int sr, int64_t length = -1);
Vec bellsClicks(size_t n, int sr, const Vec &times, const Vec &amps, double lo, Generator &rng);
void bellsDoublets(const Vec &freqs, const Vec &t60s, Generator &crng, double split, Vec &fa, Vec &fb, Vec &ta,
                   Vec &tb);
Vec deskIr(int sr, Generator &mrng, size_t k, double bandLo, double bandHi, double t60, int count);
Vec deskNoise(Generator &rng, size_t n, int sr, double lo, double hi);
struct DeskBody {
   double lo, hi, t60;
   int count;
   double gain;
};
// mseed < 0: None
Vec deskHit(int sr, Generator &rng, double length, double bandLo, double bandHi, double t60, int count = 14,
            double contact = 0.00008, double noise = 1.0, double noiseTau = 0.0006, int64_t mseed = -1,
            const DeskBody *body = nullptr);
void deskAdd(Vec &out, int64_t at, const Vec &g, double a = 1.0);

// Small vector arithmetic.
inline Vec operator+(const Vec &a, const Vec &b) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = a[i] + b[i];
   return r;
}
inline Vec operator-(const Vec &a, const Vec &b) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = a[i] - b[i];
   return r;
}
inline Vec operator*(const Vec &a, const Vec &b) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = a[i] * b[i];
   return r;
}
inline Vec operator*(double s, const Vec &a) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = s * a[i];
   return r;
}
inline Vec operator*(const Vec &a, double s) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = a[i] * s;
   return r;
}
inline Vec operator/(const Vec &a, double s) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = a[i] / s;
   return r;
}
inline Vec &operator+=(Vec &a, const Vec &b) {
   for (size_t i = 0; i < a.size(); ++i)
      a[i] += b[i];
   return a;
}
inline Vec &operator*=(Vec &a, const Vec &b) {
   for (size_t i = 0; i < a.size(); ++i)
      a[i] *= b[i];
   return a;
}
inline Vec &operator*=(Vec &a, double s) {
   for (double &x : a)
      x *= s;
   return a;
}
template <class F> Vec map(const Vec &a, F f) {
   Vec r(a.size());
   for (size_t i = 0; i < a.size(); ++i)
      r[i] = f(a[i]);
   return r;
}

} // namespace jkit::orb
