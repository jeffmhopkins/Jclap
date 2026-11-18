#include "gen/orb/dsp.h"

#include <algorithm>
#include <cmath>

#define POCKETFFT_NO_MULTITHREADING
#include <pocketfft_hdronly.h>

namespace jkit::orb {

// ---- numpy ----

size_t nextPow2(size_t n) {
   size_t p = 1;
   while (p < n)
      p <<= 1;
   return p;
}

CVec rfft(const Vec &x, size_t n) {
   Vec in(n, 0.0);
   std::copy_n(x.begin(), std::min(n, x.size()), in.begin());
   CVec out(n / 2 + 1);
   if (n == 0)
      return out;
   pocketfft::r2c<double>({n}, {sizeof(double)}, {sizeof(std::complex<double>)}, 0, true, in.data(), out.data(), 1.0);
   return out;
}

CVec rfft(const Vec &x) { return rfft(x, x.size()); }

Vec irfft(const CVec &X, size_t n) {
   CVec in(n / 2 + 1, std::complex<double>(0.0, 0.0));
   std::copy_n(X.begin(), std::min(in.size(), X.size()), in.begin());
   Vec out(n);
   if (n == 0)
      return out;
   pocketfft::c2r<double>({n}, {sizeof(std::complex<double>)}, {sizeof(double)}, 0, false, in.data(), out.data(),
                          1.0 / static_cast<double>(n));
   return out;
}

Vec rfftfreq(size_t n, double sr) {
   const double d = 1.0 / sr;
   const double val = 1.0 / (static_cast<double>(n) * d);
   Vec f(n / 2 + 1);
   for (size_t k = 0; k < f.size(); ++k)
      f[k] = static_cast<double>(k) * val;
   return f;
}

Vec linspace(double a, double b, size_t n) {
   Vec y(n);
   if (n == 0)
      return y;
   if (n == 1) {
      y[0] = a;
      return y;
   }
   const double delta = b - a;
   const double div = static_cast<double>(n - 1);
   const double step = delta / div;
   for (size_t i = 0; i < n; ++i)
      y[i] = step == 0.0 ? static_cast<double>(i) / div * delta + a : static_cast<double>(i) * step + a;
   y[n - 1] = b;
   return y;
}

Vec times(size_t n, double sr) {
   Vec t(n);
   for (size_t i = 0; i < n; ++i)
      t[i] = static_cast<double>(i) / sr;
   return t;
}

Vec hanning(size_t m) {
   if (m < 1)
      return {};
   if (m == 1)
      return {1.0};
   Vec w(m);
   for (size_t i = 0; i < m; ++i) {
      const double k = 1.0 - static_cast<double>(m) + 2.0 * static_cast<double>(i);
      w[i] = 0.5 + 0.5 * std::cos(kPi * k / static_cast<double>(m - 1));
   }
   return w;
}

Vec cumsum(const Vec &x) {
   Vec out(x.size());
   double acc = 0.0;
   for (size_t i = 0; i < x.size(); ++i)
      out[i] = acc += x[i];
   return out;
}

double interp1(double x, const Vec &xp, const Vec &fp) {
   const size_t len = xp.size();
   if (len == 0)
      return 0.0;
   if (len == 1)
      return fp[0];
   if (x > xp[len - 1])
      return fp[len - 1];
   if (x < xp[0])
      return fp[0];
   // the largest j with xp[j] <= x
   size_t lo = 0, hi = len;
   while (hi - lo > 1) {
      const size_t mid = (lo + hi) / 2;
      if (xp[mid] <= x)
         lo = mid;
      else
         hi = mid;
   }
   const size_t j = lo;
   if (j == len - 1)
      return fp[j];
   if (xp[j] == x)
      return fp[j];
   const double slope = (fp[j + 1] - fp[j]) / (xp[j + 1] - xp[j]);
   double r = slope * (x - xp[j]) + fp[j];
   if (std::isnan(r)) {
      r = slope * (x - xp[j + 1]) + fp[j + 1];
      if (std::isnan(r) && fp[j] == fp[j + 1])
         r = fp[j];
   }
   return r;
}

Vec interp(const Vec &x, const Vec &xp, const Vec &fp) {
   Vec out(x.size());
   for (size_t i = 0; i < x.size(); ++i)
      out[i] = interp1(x[i], xp, fp);
   return out;
}

namespace {
// numpy's pairwise summation (add.reduce on a contiguous float64 array).
double pairwise(const double *a, size_t n) {
   if (n < 8) {
      double res = 0.0;
      for (size_t i = 0; i < n; ++i)
         res += a[i];
      return res;
   }
   if (n <= 128) {
      double r[8];
      for (int k = 0; k < 8; ++k)
         r[k] = a[k];
      size_t i;
      for (i = 8; i < n - (n % 8); i += 8)
         for (int k = 0; k < 8; ++k)
            r[k] += a[i + k];
      double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
      for (; i < n; ++i)
         res += a[i];
      return res;
   }
   size_t n2 = n / 2;
   n2 -= n2 % 8;
   return pairwise(a, n2) + pairwise(a + n2, n - n2);
}
} // namespace

double sum(const Vec &x) { return pairwise(x.data(), x.size()); }
double mean(const Vec &x) { return x.empty() ? 0.0 : sum(x) / static_cast<double>(x.size()); }

double absMax(const Vec &x) {
   double m = 0.0;
   for (double v : x)
      m = std::max(m, std::fabs(v));
   return m;
}

size_t argmax(const Vec &x) {
   size_t best = 0;
   for (size_t i = 1; i < x.size(); ++i)
      if (x[i] > x[best])
         best = i;
   return best;
}

Vec zeros(size_t n) { return Vec(n, 0.0); }

Vec slice(const Vec &x, size_t lo, size_t hi) {
   hi = std::min(hi, x.size());
   lo = std::min(lo, hi);
   return Vec(x.begin() + static_cast<std::ptrdiff_t>(lo), x.begin() + static_cast<std::ptrdiff_t>(hi));
}

Vec padZeros(const Vec &x, size_t a, size_t b) {
   Vec out(a + x.size() + b, 0.0);
   std::copy(x.begin(), x.end(), out.begin() + static_cast<std::ptrdiff_t>(a));
   return out;
}

double clip(double x, double lo, double hi) { return std::min(std::max(x, lo), hi); }

double roundEven(double x) { return std::nearbyint(x); }

// ---- dsp.py ----

double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

Vec fade(Vec x, int sr, double fadeIn, double fadeOut) {
   const size_t n = x.size();
   const size_t a = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(1, toInt(fadeIn * sr))));
   const size_t b = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(1, toInt(fadeOut * sr))));
   const Vec up = linspace(0.0, 1.0, a), down = linspace(1.0, 0.0, b);
   for (size_t i = 0; i < a; ++i)
      x[i] *= up[i];
   for (size_t i = 0; i < b; ++i)
      x[n - b + i] *= down[i];
   return x;
}

void pan(const Vec &x, double p, Vec &left, Vec &right) {
   p = clip(p, -1.0, 1.0);
   const double angle = (p + 1.0) * kPi / 4.0;
   left = x * std::cos(angle);
   right = x * std::sin(angle);
}

void stft(const Vec &x, int nFft, int hop, std::vector<CVec> &frames) {
   const size_t n = x.size();
   const size_t pad = static_cast<size_t>(nFft / 2);
   const size_t len = n + 2 * pad + static_cast<size_t>(nFft);
   // np.pad(mode="reflect"): mirror about the end samples, repeatedly
   Vec xp(len);
   const int64_t period = 2 * (static_cast<int64_t>(n) - 1);
   for (size_t i = 0; i < len; ++i) {
      int64_t p = static_cast<int64_t>(i) - static_cast<int64_t>(pad);
      if (period <= 0) {
         xp[i] = n ? x[0] : 0.0;
         continue;
      }
      p = std::llabs(p) % period;
      if (p >= static_cast<int64_t>(n))
         p = period - p;
      xp[i] = x[static_cast<size_t>(p)];
   }
   const Vec win = hanning(static_cast<size_t>(nFft));
   const size_t count = (len - static_cast<size_t>(nFft)) / static_cast<size_t>(hop) + 1;
   frames.resize(count);
   Vec frame(static_cast<size_t>(nFft));
   for (size_t f = 0; f < count; ++f) {
      for (size_t k = 0; k < static_cast<size_t>(nFft); ++k)
         frame[k] = xp[f * static_cast<size_t>(hop) + k] * win[k];
      frames[f] = rfft(frame);
   }
}

Vec istft(const std::vector<CVec> &X, int nFft, int hop, size_t length) {
   const Vec win = hanning(static_cast<size_t>(nFft));
   const size_t N = static_cast<size_t>(nFft), H = static_cast<size_t>(hop);
   const size_t outLen = N + H * (X.size() - 1);
   Vec out(outLen, 0.0), wsum(outLen, 0.0);
   for (size_t i = 0; i < X.size(); ++i) {
      const Vec fr = irfft(X[i], N);
      const size_t s = i * H;
      for (size_t k = 0; k < N; ++k) {
         out[s + k] += fr[k] * win[k];
         wsum[s + k] += win[k] * win[k];
      }
   }
   for (size_t i = 0; i < outLen; ++i)
      out[i] /= std::max(wsum[i], 1e-8);
   const size_t pad = N / 2;
   Vec r(length, 0.0);
   for (size_t i = 0; i < length && pad + i < outLen; ++i)
      r[i] = out[pad + i];
   return r;
}

Vec bandpassTimevarying(const Vec &x, const Vec &centers, const Vec &widths, int sr, int nFft, int hop) {
   std::vector<CVec> X;
   stft(x, nFft, hop, X);
   const Vec freqs = rfftfreq(static_cast<size_t>(nFft), sr);
   const size_t nf = X.size();
   const Vec grid = linspace(0.0, 1.0, nf);
   const Vec c = interp(grid, linspace(0.0, 1.0, centers.size()), centers);
   const Vec w = interp(grid, linspace(0.0, 1.0, widths.size()), widths);
   for (size_t i = 0; i < nf; ++i)
      for (size_t k = 0; k < freqs.size(); ++k) {
         const double z = (freqs[k] - c[i]) / std::max(w[i], 1.0);
         X[i][k] *= std::exp(-0.5 * (z * z));
      }
   return istft(X, nFft, hop, x.size());
}

namespace {
// The Butterworth-shaped magnitude of bandlimit on an m-point grid.
Vec bandMag(size_t m, int sr, double lo, double hi, int order) {
   const Vec f = rfftfreq(m, sr);
   Vec mag(f.size(), 1.0);
   if (hi >= 0)
      for (size_t i = 0; i < f.size(); ++i)
         mag[i] /= std::sqrt(1.0 + std::pow(f[i] / std::max(hi, 1.0), 2 * order));
   if (lo >= 0)
      for (size_t i = 0; i < f.size(); ++i) {
         const double fr = std::max(f[i], 1e-9) / std::max(lo, 1.0);
         mag[i] /= std::sqrt(1.0 + std::pow(fr, -2 * order));
      }
   return mag;
}
} // namespace

Vec bandlimit(const Vec &x, int sr, double lo, double hi, int order, size_t pad) {
   const size_t n = x.size();
   if (n == 0 || (lo < 0 && hi < 0))
      return x;
   const size_t m = n + 2 * pad;
   CVec X = rfft(padZeros(x, pad, pad), m);
   const Vec mag = bandMag(m, sr, lo, hi, order);
   for (size_t i = 0; i < X.size(); ++i)
      X[i] *= mag[i];
   return slice(irfft(X, m), pad, pad + n);
}

Vec shapedBurst(Generator &rng, size_t n, int64_t k, double decay, int sr, double lo, double hi) {
   Vec out(n, 0.0);
   const size_t kk = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(1, k)));
   const Vec env = linspace(0.0, decay, kk);
   Vec raw(kk);
   for (size_t i = 0; i < kk; ++i)
      raw[i] = rng.normal();
   for (size_t i = 0; i < kk; ++i)
      raw[i] *= std::exp(-env[i]);
   std::copy(raw.begin(), raw.end(), out.begin());
   Vec y = bandlimit(out, sr, lo, hi);
   const double ey = sum(y * y);
   if (ey > 1e-24)
      y *= std::sqrt(sum(raw * raw) / ey);
   return y;
}

Vec modal(double f0, double dur, int sr, const Vec &ratios, const Vec &t60, const Vec &gains) {
   const size_t n = static_cast<size_t>(std::max<int64_t>(1, toInt(dur * sr)));
   const Vec t = times(n, sr);
   Vec out(n, 0.0);
   const size_t count = std::min({ratios.size(), t60.size(), gains.size()});
   for (size_t j = 0; j < count; ++j) {
      const double f = f0 * ratios[j];
      if (f >= sr * 0.45)
         continue;
      const double d = std::max(t60[j], 1e-3);
      for (size_t i = 0; i < n; ++i)
         out[i] += gains[j] * std::exp(-kLn1000 * t[i] / d) * std::sin(kTau * f * t[i]);
   }
   return out;
}

Vec sweepSine(double f0, double f1, double dur, int sr, double curve) {
   const size_t n = static_cast<size_t>(std::max<int64_t>(1, toInt(dur * sr)));
   const Vec t = times(n, sr);
   Vec f(n);
   for (size_t i = 0; i < n; ++i)
      f[i] = f0 + (f1 - f0) * std::pow(t[i] / std::max(dur, 1e-6), curve);
   const Vec c = cumsum(f);
   Vec out(n);
   for (size_t i = 0; i < n; ++i)
      out[i] = std::sin(kTau * c[i] / sr);
   return out;
}

Vec envApex(size_t n, int sr, double apexS, double risePower, double fallPower) {
   const Vec t = times(n, sr);
   const double a = std::max(apexS, 1e-3);
   const double d = std::max(static_cast<double>(n) / sr - a, 1e-3);
   Vec out(n);
   for (size_t i = 0; i < n; ++i) {
      const double rise = std::pow(clip(t[i] / a, 0.0, 1.0), risePower);
      const double fall = std::exp(-fallPower * std::max(t[i] - a, 0.0) / d);
      out[i] = rise * fall;
   }
   return out;
}

Vec saturate(const Vec &x, double drive) {
   const double td = std::tanh(drive);
   return map(x, [&](double v) { return std::tanh(v * drive) / td; });
}

Vec alignPeak(const Vec &x, int sr, double targetS, double maxShift) {
   const size_t n = x.size();
   if (n == 0)
      return x;
   const size_t k = static_cast<size_t>(std::max<int64_t>(1, toInt(0.003 * sr)));
   // np.convolve(|x|, ones(k) / k, "same"): same[i] = full[i + (k - 1) // 2]
   const double v = 1.0 / static_cast<double>(k);
   const int64_t off = static_cast<int64_t>((k - 1) / 2);
   Vec env(n, 0.0);
   for (size_t i = 0; i < n; ++i) {
      const int64_t full = static_cast<int64_t>(i) + off;
      double acc = 0.0;
      for (size_t j = 0; j < k; ++j) {
         const int64_t a = full - static_cast<int64_t>(j);
         if (a >= 0 && a < static_cast<int64_t>(n))
            acc += std::fabs(x[static_cast<size_t>(a)]) * v;
      }
      env[i] = acc;
   }
   const int64_t peak = static_cast<int64_t>(argmax(env));
   const int64_t target = toInt(clip(targetS, 0.0, static_cast<double>(n) / sr) * sr);
   const int64_t shift = target - peak;
   if (static_cast<double>(std::llabs(shift)) > maxShift * sr)
      return x;
   Vec out(n, 0.0);
   for (size_t i = 0; i < n; ++i) {
      const int64_t src = static_cast<int64_t>(i) - shift;
      if (src >= 0 && src < static_cast<int64_t>(n))
         out[i] = x[static_cast<size_t>(src)];
   }
   return out;
}

Vec reverb(const Vec &x, int sr, double decay, double wet, uint64_t seed, bool tail) {
   if (wet <= 0 || x.empty())
      return x;
   Generator rng(seed);
   const size_t nIr = static_cast<size_t>(std::max<int64_t>(8, toInt(decay * 2.5 * sr)));
   const Vec t = times(nIr, sr);
   const double d = std::max(decay, 1e-3);
   const Vec noise = bandlimit(rng.normal(nIr), sr, 80.0, 7000.0, 4);
   const Vec low = bandlimit(noise, sr, -1, 3000.0, 2);
   Vec ir(nIr);
   for (size_t i = 0; i < nIr; ++i) {
      const double high = noise[i] - low[i];
      ir[i] = low[i] * std::exp(-kLn1000 * t[i] / d) + 0.5 * high * std::exp(-kLn1000 * t[i] / (0.4 * d));
   }
   const size_t ramp = static_cast<size_t>(std::max<int64_t>(1, toInt(0.01 * sr)));
   const Vec up = linspace(0.0, 1.0, ramp);
   for (size_t i = 0; i < ramp && i < nIr; ++i)
      ir[i] *= up[i];
   const double e = std::sqrt(sum(ir * ir)) + 1e-9;
   for (double &v : ir)
      v /= e;
   const size_t n = x.size() + nIr - 1;
   const size_t nfft = nextPow2(n);
   CVec X = rfft(x, nfft);
   const CVec I = rfft(ir, nfft);
   for (size_t i = 0; i < X.size(); ++i)
      X[i] *= I[i];
   const Vec wetSig = irfft(X, nfft);
   const size_t len = tail ? n : x.size();
   Vec out(len);
   for (size_t i = 0; i < len; ++i)
      out[i] = (1.0 - wet) * (i < x.size() ? x[i] : 0.0) + wet * wetSig[i];
   return out;
}

// ---- foley.py helpers ----

Vec fftconv(const Vec &a, const Vec &b, size_t nOut) {
   const size_t m = a.size() + b.size() - 1;
   const size_t nfft = nextPow2(m);
   CVec A = rfft(a, nfft);
   const CVec B = rfft(b, nfft);
   for (size_t i = 0; i < A.size(); ++i)
      A[i] *= B[i];
   Vec y = irfft(A, nfft);
   y.resize(std::min(nOut, y.size()));
   return y;
}

Vec modesIr(const Vec &freqs, const Vec &t60s, const Vec &gains, int sr, size_t nMax) {
   const double maxT60 = *std::max_element(t60s.begin(), t60s.end());
   const size_t n = static_cast<size_t>(
      toInt(std::min(static_cast<double>(nMax), std::max(16.0, 1.1 * maxT60 * sr))));
   const Vec t = times(n, sr);
   Vec ir(n, 0.0);
   const size_t count = std::min({freqs.size(), t60s.size(), gains.size()});
   for (size_t j = 0; j < count; ++j) {
      const double f = freqs[j];
      if (!(20.0 < f && f < 0.45 * sr))
         continue;
      const double d = std::max(t60s[j], 1e-3);
      for (size_t i = 0; i < n; ++i)
         ir[i] += gains[j] * std::exp(-kLn1000 * t[i] / d) * std::sin(kTau * f * t[i]);
   }
   return ir;
}

Vec ring(const Vec &exc, const Vec &freqs, const Vec &t60s, const Vec &gains, int sr) {
   const size_t n = exc.size();
   Vec d1(n), acc(n);
   double prev = 0.0;
   for (size_t i = 0; i < n; ++i) {
      d1[i] = exc[i] - prev;
      prev = exc[i];
   }
   prev = 0.0;
   for (size_t i = 0; i < n; ++i) {
      acc[i] = d1[i] - prev;
      prev = d1[i];
   }
   return fftconv(acc, modesIr(freqs, t60s, gains, sr, n), n);
}

Vec burst(Generator &rng, size_t n, int64_t at, int64_t length, double tau, int sr, double rise) {
   Vec out(n, 0.0);
   if (at >= static_cast<int64_t>(n))
      return out;
   const size_t k = static_cast<size_t>(std::max<int64_t>(1, std::min<int64_t>(length, static_cast<int64_t>(n) - at)));
   const Vec t = times(k, sr);
   Vec env(k);
   const double tt = std::max(tau, 1e-4);
   for (size_t i = 0; i < k; ++i)
      env[i] = std::exp(-t[i] / tt);
   const size_t r = std::min<size_t>(k, static_cast<size_t>(std::max<int64_t>(1, toInt(rise * sr))));
   for (size_t i = 0; i < r; ++i)
      env[i] *= 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(r));
   for (size_t i = 0; i < k; ++i)
      out[static_cast<size_t>(at) + i] = rng.normal() * env[i];
   return out;
}

Vec pulse(size_t n, int64_t at, int64_t width) {
   Vec out(n, 0.0);
   if (at >= static_cast<int64_t>(n))
      return out;
   const int64_t k = std::max<int64_t>(2, std::min<int64_t>(width, static_cast<int64_t>(n) - at));
   for (int64_t i = 0; i < k && at + i < static_cast<int64_t>(n); ++i)
      out[static_cast<size_t>(at + i)] = std::sin(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(k));
   return out;
}

Vec smooth(Generator &rng, size_t n, int sr, double rate) {
   const int64_t k = std::max<int64_t>(2, toInt(static_cast<double>(n) / sr * rate) + 2);
   const Vec pts = rng.uniform(0.0, 1.0, static_cast<size_t>(k));
   const Vec x = linspace(0.0, static_cast<double>(k) - 1.0, n);
   Vec out(n);
   for (size_t j = 0; j < n; ++j) {
      const int64_t i = std::min<int64_t>(static_cast<int64_t>(std::floor(x[j])), k - 2);
      const double w = 0.5 - 0.5 * std::cos(kPi * (x[j] - static_cast<double>(i)));
      out[j] = pts[static_cast<size_t>(i)] * (1.0 - w) + pts[static_cast<size_t>(i + 1)] * w;
   }
   return out;
}

Vec fadeOutEnv(size_t n, int sr, double length) {
   Vec env(n, 1.0);
   const size_t b = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(2, toInt(length * sr))));
   const Vec l = linspace(0.0, kPi, b);
   for (size_t i = 0; i < b; ++i)
      env[n - b + i] = 0.5 + 0.5 * std::cos(l[i]);
   return env;
}

Vec norm(const Vec &x) {
   const double m = absMax(x);
   return m < 1e-12 ? x : x / m;
}

Vec attack(size_t n, int sr, double at, double rise) {
   const Vec t = times(n, sr);
   const double r = std::max(rise, 1e-4);
   return map(t, [&](double v) { return 0.5 - 0.5 * std::cos(kPi * clip((v - at) / r, 0.0, 1.0)); });
}

Vec smoothLowpass(const Vec &x, int sr, double fPass, double fStop, size_t pad) {
   const size_t n = x.size();
   if (n == 0)
      return x;
   const size_t m = n + 2 * pad;
   const Vec f = rfftfreq(m, sr);
   CVec X = rfft(padZeros(x, pad, pad), m);
   for (size_t i = 0; i < X.size(); ++i) {
      const double u = clip((f[i] - fPass) / (fStop - fPass), 0.0, 1.0);
      X[i] *= 0.5 + 0.5 * std::cos(kPi * u);
   }
   return slice(irfft(X, m), pad, pad + n);
}

Vec foleyEdges(Vec x, int sr) {
   const size_t n = x.size();
   const size_t a = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(2, toInt(0.0015 * sr))));
   const size_t b = std::min<size_t>(n, static_cast<size_t>(std::max<int64_t>(2, toInt(0.012 * sr))));
   const Vec la = linspace(0.0, kPi, a), lb = linspace(0.0, kPi, b);
   for (size_t i = 0; i < a; ++i)
      x[i] *= 0.5 - 0.5 * std::cos(la[i]);
   for (size_t i = 0; i < b; ++i)
      x[n - b + i] *= 0.5 + 0.5 * std::cos(lb[i]);
   return x;
}

Vec finish(const Vec &x, int sr) {
   Vec y = smoothLowpass(bandlimit(x, sr, 20.0, -1, 4), sr);
   const double m = mean(y);
   for (double &v : y)
      v -= m;
   return foleyEdges(std::move(y), sr);
}

// ---- natural.py helpers ----

Vec strike(size_t n, int sr, int64_t at, const Vec &freqs, const Vec &t60s, const Vec &gains, Generator &rng,
           double contact, double noise) {
   Vec exc = pulse(n, at, std::max<int64_t>(2, toInt(contact * sr)));
   if (noise > 0) {
      const Vec b = burst(rng, n, at, toInt(4 * contact * sr) + 2, contact, sr, 0.0005);
      for (size_t i = 0; i < n; ++i)
         exc[i] = exc[i] + noise * b[i];
   }
   return fftconv(exc, modesIr(freqs, t60s, gains, sr, n), n);
}

Vec grains(Generator &rng, size_t n, const Vec &times_, const Vec &amps, int sr) {
   Vec out(n, 0.0);
   std::vector<size_t> idx;
   std::vector<double> a;
   for (size_t i = 0; i < times_.size(); ++i) {
      const int64_t j = toInt(times_[i] * sr);
      if (j >= 0 && j < static_cast<int64_t>(n)) {
         idx.push_back(static_cast<size_t>(j));
         a.push_back(amps[i]);
      }
   }
   const Vec signs = rng.choice({-1.0, 1.0}, idx.size());
   for (size_t i = 0; i < idx.size(); ++i)
      out[idx[i]] += a[i] * signs[i];
   return out;
}

Vec decorrelate(const Vec &x, int sr, Generator &crng, int taps, double span) {
   const size_t n = x.size();
   Vec out = x;
   for (int k = 0; k < taps; ++k) {
      const int64_t d = toInt(crng.uniform(0.002, span) * sr);
      if (d < static_cast<int64_t>(n)) {
         const double g = crng.uniform(0.05, 0.15);
         for (size_t i = static_cast<size_t>(d); i < n; ++i)
            out[i] += g * x[i - static_cast<size_t>(d)];
      }
   }
   return out;
}

size_t waterFast(size_t m) {
   size_t best = m <= 1 ? 1 : nextPow2(m);
   for (size_t p5 = 1; p5 < best; p5 *= 5)
      for (size_t p35 = p5; p35 < best; p35 *= 3) {
         size_t q = p35;
         while (q < m)
            q *= 2;
         best = std::min(best, q);
      }
   return best;
}

Vec waterBand(const Vec &x, int sr, double lo, double hi, int order, size_t pad) {
   const size_t n = x.size();
   const size_t m = waterFast(n + 2 * pad);
   CVec X = rfft(padZeros(x, pad, 0), m);
   const Vec mag = bandMag(m, sr, lo, hi, order);
   for (size_t i = 0; i < X.size(); ++i)
      X[i] *= mag[i];
   return slice(irfft(X, m), pad, pad + n);
}

Vec waterNoise(Generator &rng, size_t n, int sr, double lo, double hi, int order) {
   const size_t m = waterFast(n);
   const size_t bins = m / 2 + 1;
   const Vec re = rng.normal(bins);
   const Vec im = rng.normal(bins);
   const Vec mag = bandMag(m, sr, lo, hi, order);
   CVec spec(bins);
   for (size_t i = 0; i < bins; ++i)
      spec[i] = std::complex<double>(re[i], im[i]) * mag[i];
   Vec y = irfft(spec, m);
   y.resize(n);
   return norm(y);
}

Vec waterBubbles(size_t n, int sr, const Vec &times_, const Vec &f0sIn, const Vec &ampsIn, double damp, double rise,
                 int dec, double floor) {
   const double sr2 = static_cast<double>(sr) / dec;
   const size_t n2 = waterFast((n + static_cast<size_t>(dec) - 1) / static_cast<size_t>(dec) + 64);
   Vec out2(n2, 0.0);
   const double fMax = 0.42 * sr2 / (1.0 + floor * rise);
   Vec t, f0s, amps;
   for (size_t i = 0; i < times_.size(); ++i) {
      const double f0 = std::min(f0sIn.size() == 1 ? f0sIn[0] : f0sIn[i], fMax);
      const double a = ampsIn.size() == 1 ? ampsIn[0] : ampsIn[i];
      if (times_[i] >= 0 && times_[i] < static_cast<double>(n) / sr && a > 0 && f0 > 20) {
         t.push_back(times_[i]);
         f0s.push_back(f0);
         amps.push_back(a);
      }
   }
   if (!t.empty()) {
      const size_t count = t.size();
      Vec d(count);
      std::vector<int> cls(count);
      std::vector<int64_t> start(count);
      for (size_t i = 0; i < count; ++i) {
         d[i] = (0.043 * f0s[i] + 0.0014 * std::pow(f0s[i], 1.5)) * damp;
         const int64_t L = std::max<int64_t>(16, static_cast<int64_t>(std::ceil(floor / d[i] * sr2)));
         cls[i] = static_cast<int>(std::ceil(std::log2(static_cast<double>(L))));
         start[i] = toInt(t[i] * sr2);
      }
      const size_t r = static_cast<size_t>(std::max<int64_t>(2, toInt(0.001 * sr2)));
      Vec ramp(r);
      for (size_t i = 0; i < r; ++i)
         ramp[i] = 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(r));
      std::vector<int> classes(cls);
      std::sort(classes.begin(), classes.end());
      classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
      Vec tmp(n2);
      for (int c : classes) {
         std::vector<size_t> idx;
         for (size_t i = 0; i < count; ++i)
            if (cls[i] == c)
               idx.push_back(i);
         const size_t Lc = size_t(1) << c;
         const Vec tt = times(Lc, sr2);
         const size_t step = std::max<size_t>(1, 1500000 / Lc);
         for (size_t b = 0; b < idx.size(); b += step) {
            std::fill(tmp.begin(), tmp.end(), 0.0);
            for (size_t q = b; q < std::min(idx.size(), b + step); ++q) {
               const size_t j = idx[q];
               const double dj = d[j];
               for (size_t s = 0; s < Lc; ++s) {
                  const int64_t pos = start[j] + static_cast<int64_t>(s);
                  if (pos >= static_cast<int64_t>(n2))
                     break;
                  double y = std::sin(kTau * f0s[j] * (tt[s] + 0.5 * rise * dj * tt[s] * tt[s])) *
                             std::exp(-dj * tt[s]) * amps[j];
                  if (s < r)
                     y *= ramp[s];
                  tmp[static_cast<size_t>(pos)] += y;
               }
            }
            out2 += tmp;
         }
      }
   }
   const size_t N = n2 * static_cast<size_t>(dec);
   CVec Z(N / 2 + 1, std::complex<double>(0.0, 0.0));
   const CVec Y = rfft(out2);
   std::copy(Y.begin(), Y.end(), Z.begin());
   Vec y = irfft(Z, N);
   y.resize(n);
   for (double &v : y)
      v *= dec;
   return y;
}

Vec waterTimes(Generator &crng, const Vec &rate, int sr, int64_t cap) {
   Vec cum = cumsum(rate);
   for (double &c : cum)
      c /= sr;
   const double total = cum.empty() ? 0.0 : cum.back();
   const int64_t k = std::min<int64_t>(cap, crng.poisson(std::max(total, 0.0)));
   if (k == 0)
      return {};
   Vec u = crng.uniform(0.0, total, static_cast<size_t>(k));
   std::sort(u.begin(), u.end());
   return interp(u, cum, times(rate.size(), sr));
}

Vec waterCrackle(Generator &rng, Generator &crng, size_t n, int sr, const Vec &rate, double lo, double hi, double alpha,
                 int order) {
   const Vec tt = waterTimes(crng, rate, sr);
   Vec amps(tt.size());
   for (double &a : amps)
      a = std::min(crng.pareto(alpha) + 1.0, 30.0);
   return waterBand(grains(rng, n, tt, amps, sr), sr, lo, hi, order);
}

Vec waterRadii(Generator &crng, size_t k, double rLo, double rHi, double slope) {
   const Vec u = crng.uniform(0.0, 1.0, k);
   const double a = 1.0 - slope;
   const double pa = std::pow(rLo, a), pb = std::pow(rHi, a);
   return map(u, [&](double v) { return std::pow(pa + v * (pb - pa), 1.0 / a); });
}

void bellsPlace(Vec &out, int64_t at, const Vec &x, double amp) {
   const int64_t n = static_cast<int64_t>(out.size());
   if (at >= n || at < 0 || amp == 0)
      return;
   const size_t k = std::min<size_t>(x.size(), static_cast<size_t>(n - at));
   for (size_t i = 0; i < k; ++i)
      out[static_cast<size_t>(at) + i] += amp * x[i];
}

Vec bellsMode(double f, double t60, int sr, size_t k, size_t block) {
   const std::complex<double> z = std::complex<double>(-kLn1000 / std::max(t60, 1e-3), kTau * f) / static_cast<double>(sr);
   const size_t b = std::min(block, k);
   CVec base(b);
   for (size_t i = 0; i < b; ++i)
      base[i] = std::exp(std::complex<double>(z.real() * static_cast<double>(i), z.imag() * static_cast<double>(i)));
   const size_t nb = (k + b - 1) / b;
   const std::complex<double> zb(z.real() * static_cast<double>(b), z.imag() * static_cast<double>(b));
   Vec out(k);
   for (size_t s = 0; s < nb; ++s) {
      const std::complex<double> st =
         std::exp(std::complex<double>(zb.real() * static_cast<double>(s), zb.imag() * static_cast<double>(s)));
      for (size_t i = 0; i < b && s * b + i < k; ++i)
         out[s * b + i] = st.real() * base[i].imag() + st.imag() * base[i].real();
   }
   return out;
}

Vec bellsModes(const Vec &freqs, const Vec &t60sIn, const Vec &gains, int sr, int64_t length) {
   Vec t60s = map(t60sIn, [](double v) { return std::max(v, 1e-3); });
   const int64_t big = toInt(1.1 * *std::max_element(t60s.begin(), t60s.end()) * sr) + 1;
   const size_t m = static_cast<size_t>(length < 0 ? big : std::min(length, big));
   Vec out(m, 0.0);
   const size_t count = std::min({freqs.size(), t60s.size(), gains.size()});
   for (size_t j = 0; j < count; ++j) {
      const double f = freqs[j];
      if (!(20.0 < f && f < 0.45 * sr) || gains[j] == 0)
         continue;
      const size_t k = std::min<size_t>(m, static_cast<size_t>(toInt(1.1 * t60s[j] * sr) + 1));
      const Vec mode = bellsMode(f, t60s[j], sr, k);
      for (size_t i = 0; i < k; ++i)
         out[i] += gains[j] * mode[i];
   }
   return out;
}

Vec bellsClicks(size_t n, int sr, const Vec &times_, const Vec &amps, double lo, Generator &rng) {
   const size_t h = 512;
   Vec kern(2 * h, 0.0);
   kern[h] = 1.0;
   kern = bandlimit(kern, sr, lo, -1, 2, h) * hanning(2 * h);
   Vec out(n + 2 * h, 0.0);
   const Vec signs = rng.choice({-1.0, 1.0}, times_.size());
   for (size_t i = 0; i < times_.size(); ++i) {
      const int64_t at = toInt(times_[i] * sr);
      if (at >= 0 && at < static_cast<int64_t>(n)) {
         const double g = amps[i] * signs[i];
         for (size_t q = 0; q < 2 * h; ++q)
            out[static_cast<size_t>(at) + q] += g * kern[q];
      }
   }
   return slice(out, h, h + n);
}

void bellsDoublets(const Vec &freqs, const Vec &t60s, Generator &crng, double split, Vec &fa, Vec &fb, Vec &ta,
                   Vec &tb) {
   const size_t n = freqs.size();
   const Vec u = crng.uniform(0.3, 1.0, n);
   fa.resize(n);
   fb.resize(n);
   for (size_t i = 0; i < n; ++i) {
      const double d = freqs[i] * split * u[i];
      fa[i] = freqs[i] - 0.5 * d;
      fb[i] = freqs[i] + 0.5 * d;
   }
   const Vec ua = crng.uniform(0.9, 1.1, n);
   ta = t60s * ua;
   const Vec ub = crng.uniform(0.9, 1.1, n);
   tb = t60s * ub;
}

Vec deskIr(int sr, Generator &mrng, size_t k, double bandLo, double bandHi, double t60, int count) {
   const Vec t = times(k, sr);
   const Vec lf = mrng.uniform(std::log(bandLo), std::log(bandHi), static_cast<size_t>(count));
   const Vec fs = map(lf, [](double v) { return std::exp(v); });
   const Vec ds = t60 * mrng.uniform(0.5, 1.5, static_cast<size_t>(count));
   const Vec gs = mrng.uniform(0.4, 1.0, static_cast<size_t>(count));
   const Vec ph = mrng.uniform(0.0, kTau, static_cast<size_t>(count));
   Vec acc(k, 0.0);
   for (int j = 0; j < count; ++j) {
      const size_t J = static_cast<size_t>(j);
      for (size_t i = 0; i < k; ++i) {
         const double v = gs[J] * std::exp(-kLn1000 * t[i] / ds[J]) * std::sin(kTau * fs[J] * t[i] + ph[J]);
         acc[i] = j == 0 ? v : acc[i] + v;
      }
   }
   return norm(acc);
}

Vec deskNoise(Generator &rng, size_t n, int sr, double lo, double hi) {
   size_t m = 16;
   while (m < n)
      m <<= 1;
   const Vec f = rfftfreq(m, sr);
   CVec X = rfft(rng.normal(m));
   for (size_t i = 0; i < X.size(); ++i) {
      const double mag = 1.0 / std::sqrt(1.0 + std::pow(f[i] / hi, 4)) /
                         std::sqrt(1.0 + std::pow(std::max(f[i], 1e-9) / lo, -4));
      X[i] *= mag;
   }
   Vec y = irfft(X, m);
   y.resize(n);
   return norm(y);
}

Vec deskHit(int sr, Generator &rng, double length, double bandLo, double bandHi, double t60, int count, double contact,
            double noise, double noiseTau, int64_t mseed, const DeskBody *body) {
   const size_t k = static_cast<size_t>(std::max<int64_t>(32, toInt(length * sr)));
   const Vec t = times(k, sr);
   Vec exc(k, 0.0);
   const size_t c = static_cast<size_t>(std::max<int64_t>(2, toInt(contact * sr)));
   for (size_t i = 0; i < c && i < k; ++i)
      exc[i] = std::sin(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(c));
   if (noise > 0) {
      const Vec w = rng.normal(k);
      for (size_t i = 0; i < k; ++i)
         exc[i] += noise * w[i] * std::exp(-t[i] / noiseTau);
   }
   Vec ir;
   if (mseed < 0) {
      ir = deskIr(sr, rng, k, bandLo, bandHi, t60, count);
   } else {
      Generator m(static_cast<uint64_t>(mseed));
      ir = deskIr(sr, m, k, bandLo, bandHi, t60, count);
   }
   if (body) {
      Generator m(static_cast<uint64_t>((mseed < 0 ? 0 : mseed) + 1));
      const Vec b = deskIr(sr, m, k, body->lo, body->hi, body->t60, body->count);
      for (size_t i = 0; i < k; ++i)
         ir[i] = ir[i] + body->gain * b[i];
   }
   const size_t m = nextPow2(2 * k);
   CVec E = rfft(exc, m);
   const CVec I = rfft(ir, m);
   for (size_t i = 0; i < E.size(); ++i)
      E[i] *= I[i];
   Vec y = irfft(E, m);
   y.resize(k);
   const size_t f = std::max<size_t>(2, k / 6);
   const Vec l = linspace(0.0, kPi, f);
   for (size_t i = 0; i < f; ++i)
      y[k - f + i] *= 0.5 + 0.5 * std::cos(l[i]);
   return norm(y);
}

void deskAdd(Vec &out, int64_t at, const Vec &g, double a) {
   if (at >= 0 && at < static_cast<int64_t>(out.size())) {
      const size_t k = std::min<size_t>(g.size(), out.size() - static_cast<size_t>(at));
      for (size_t i = 0; i < k; ++i)
         out[static_cast<size_t>(at) + i] += a * g[i];
   }
}

} // namespace jkit::orb
