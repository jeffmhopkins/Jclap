// A bore as its resonances: the input impedance of a real instrument's air column (computed from
// its geometry by tools/bore.py, which bakes bore_table.h) as a sum of modes, each a complex
// one-pole driven by the flow into it, and the sound radiated from the bell as the same modes read
// out with other weights plus the wave that leaves straight through the bell above its cutoff.
//
// For each mode n (a pole s_n and its conjugate), with the flow u in m^3/s:
//    X_n[k] = z_n X_n[k-1] + u[k],   z_n = exp(s_n / fs)
//    p[k]   = Zc (r0 u[k] + sum 2 Re(c_n X_n[k]) / fs)        the pressure at the input, Pa
//    y[k]   = sum 2 Re(d_n X_n[k]) / fs + sum_j h_j u[k - n0 - j]   the radiated sound
// The input's pressure is split into what the past flow left ringing (history(), before this
// sample's flow is known) and what this sample's flow adds at once (instant() times it), so a
// reed or lips can solve their flow with the pressure it makes, in closed form.
//
// The whole bore can be scaled in frequency (a longer or shorter instrument of the same shape):
// the poles and residues by the scale, the bell's straight-through wave delayed by its inverse.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace jbreath {

struct BoreMode {
   float re, im; // the pole, rad/s
   float zr, zi; // its residue in the input impedance (over Zc), rad/s
   float rr, ri; // its residue in the radiated sound
};

inline constexpr int kBoreModes = 24, kBoreTaps = 256;
inline constexpr double kBoreTapRate = 96000.0;

struct BoreFingering {
   int modes;
   float r0;     // the impedance's constant (over Zc): the highs, where the bore looks like a resistance
   double start; // the bell's straight-through wave: when its first tap is, s
   double zc;    // the characteristic impedance at the input, Pa s / m^3
   float peakHz[kBoreModes];
   BoreMode mode[kBoreModes];
   float fir[kBoreTaps]; // the straight-through wave, at kBoreTapRate
   int taps;             // how many of its taps there are
};

class ModalBore {
public:
   static constexpr int kMaxTaps = 768, kHistory = 8192;

   void reset() {
      for (auto &x : x_)
         x = 0.0;
      std::fill(std::begin(hist_), std::end(hist_), 0.0f);
      pos_ = 0;
   }
   // The fingering's bore, scaled in frequency by `scale`, at `fs`. Its straight-through wave is
   // only resampled when the scale has moved by more than a couple of percent (a bend or a
   // wander leaves it be). Every resonance's damping can be multiplied by `loss` (a rougher,
   // lossier tube), and the bell's cutoff moved from `cutHz` to `toHz`: the resonances above it
   // are damped as much more (or less) as a bell cutting off there would.
   // (And the resonances under lowHz damped lowDamp times as much: a register vent's leak; those
   // over highHz highDamp times: a player's voicing.)
   void set(const BoreFingering &b, double scale, double fs, double loss = 1.0, double cutHz = 1500.0,
            double toHz = 1500.0, double lowHz = 0.0, double lowDamp = 1.0, double highHz = 0.0,
            double highDamp = 1.0) {
      const bool newBore = &b != bore_ || fs != fs_;
      bore_ = &b;
      fs_ = fs;
      n_ = b.modes;
      double inst = b.r0;
      for (int i = 0; i < n_; ++i) {
         const BoreMode &m = b.mode[i];
         double damp = loss;
         if (toHz != cutHz) {
            const double f = m.im * scale / (2.0 * 3.14159265358979), a = f / toHz, c = f / cutHz;
            damp *= std::sqrt((1.0 + a * a * a * a) / (1.0 + c * c * c * c));
         }
         if (m.im * scale < 2.0 * 3.14159265358979 * lowHz)
            damp *= lowDamp;
         if (highHz > 0.0 && m.im * scale > 2.0 * 3.14159265358979 * highHz)
            damp *= highDamp;
         const std::complex<double> s(m.re * scale * damp, m.im * scale);
         z_[i] = std::exp(s / fs);
         c_[i] = std::complex<double>(m.zr, m.zi) * (scale / fs);
         d_[i] = std::complex<double>(m.rr, m.ri) * (scale / fs);
         inst += 2.0 * c_[i].real();
      }
      zc_ = b.zc;
      inst_ = zc_ * inst;
      if (newBore || std::fabs(std::log(scale / firScale_)) > 0.02)
         resample(b, scale);
   }
   // The pressure the past flow leaves at the input this sample (it decays the modes on a step).
   double history() {
      std::complex<double> a = 0.0;
      for (int i = 0; i < n_; ++i) {
         x_[i] *= z_[i];
         a += c_[i] * x_[i];
      }
      return zc_ * 2.0 * a.real();
   }
   // What each m^3/s of this sample's flow adds to the input's pressure at once.
   double instant() const { return inst_; }
   // This sample's flow, once solved: it rings the modes and starts down the bell.
   void push(double u) {
      for (int i = 0; i < n_; ++i)
         x_[i] += u;
      hist_[pos_] = static_cast<float>(u);
   }
   // The sound radiated this sample (after push()).
   double radiated() {
      std::complex<double> a = 0.0;
      for (int i = 0; i < n_; ++i)
         a += d_[i] * x_[i];
      double y = 2.0 * a.real();
      int j = pos_ - n0_;
      for (int t = 0; t < taps_; ++t, --j)
         y += fir_[t] * hist_[j & (kHistory - 1)];
      pos_ = (pos_ + 1) & (kHistory - 1);
      return y;
   }
   int modes() const { return n_; }

private:
   // The straight-through wave's taps at this rate and scale: each of the table's taps (impulses
   // at kBoreTapRate) moved to its scaled time and spread over the new rate's samples by a
   // windowed sinc, which keeps it band-limited as it was.
   void resample(const BoreFingering &b, double scale) {
      firScale_ = scale;
      const double t0 = b.start / scale, span = b.taps / kBoreTapRate / scale;
      // (A lower new rate than the table's would need the sinc widened; voices run at 88.2 kHz or
      // more, where it's within a tenth of the table's.)
      const double ratio = std::min(1.0, fs_ / kBoreTapRate);
      constexpr int kHalf = 8;
      n0_ = std::max(1, static_cast<int>(std::floor(t0 * fs_)) - kHalf);
      taps_ = std::min(kMaxTaps, static_cast<int>(std::ceil(span * fs_)) + 2 * kHalf + 1);
      std::fill(std::begin(fir_), std::end(fir_), 0.0f);
      for (int k = 0; k < b.taps; ++k) {
         const double at = (b.start + k / kBoreTapRate) / scale * fs_ - n0_;
         const int lo = static_cast<int>(std::ceil(at - kHalf)), hi = static_cast<int>(std::floor(at + kHalf));
         for (int m = std::max(0, lo); m <= hi && m < taps_; ++m) {
            const double d = m - at, win = 0.5 + 0.5 * std::cos(3.14159265358979 * d / kHalf);
            const double sinc = std::fabs(d) < 1e-9 ? 1.0 : std::sin(3.14159265358979 * ratio * d) / (3.14159265358979 * ratio * d);
            fir_[m] += static_cast<float>(b.fir[k] * ratio * sinc * win);
         }
      }
   }

   const BoreFingering *bore_ = nullptr;
   double fs_ = 0.0, zc_ = 1.0, inst_ = 0.0, firScale_ = 1.0;
   int n_ = 0, n0_ = 1, taps_ = 0, pos_ = 0;
   std::complex<double> z_[kBoreModes], c_[kBoreModes], d_[kBoreModes], x_[kBoreModes];
   float fir_[kMaxTaps] = {};
   float hist_[kHistory] = {};
};

} // namespace jbreath
