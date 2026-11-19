#pragma once

// A pad's tone: up to six points, each a filter of its own kind (a bell, a shelf, a low or high
// pass, a notch or a band pass) at a frequency, gain and Q. The same coefficients play the sound and
// draw the curve, so what the PAD TONE graph shows is what the pad does.
//
// Every kind is one or two biquads (RBJ's cookbook). A 24 dB low or high pass is two, tuned as a
// Butterworth pair with the point's Q as the resonance on the second.

#include <algorithm>
#include <cmath>
#include <complex>

namespace jkit {

constexpr int kTonePoints = 6;

enum ToneType {
   kToneOff,
   kToneBell,
   kToneLowShelf,
   kToneHighShelf,
   kToneLp12,
   kToneLp24,
   kToneHp12,
   kToneHp24,
   kToneNotch,
   kToneBandPass,
   kToneTypeCount
};
inline const char *const kToneTypeNames[] = {"Off",   "Bell",  "Low shelf", "High shelf", "LP 12",
                                              "LP 24", "HP 12", "HP 24",     "Notch",      "Band pass"};

// Whether a kind has a gain (the rest ignore it), and whether the envelope and velocity move it.
inline bool toneHasGain(int type) { return type == kToneBell || type == kToneLowShelf || type == kToneHighShelf; }
inline bool toneIsFilter(int type) {
   return type == kToneLp12 || type == kToneLp24 || type == kToneHp12 || type == kToneHp24 || type == kToneBandPass;
}

// The curves: frequency 20 Hz to 20 kHz, Q 0.1 to 18 (0.71 is a plain low or high pass).
inline double toneHz(double v) { return 20.0 * std::pow(1000.0, std::clamp(v, 0.0, 1.0)); }
inline double toneHzValue(double hz) { return std::clamp(std::log(hz / 20.0) / std::log(1000.0), 0.0, 1.0); }
inline double toneQ(double v) { return 0.1 * std::pow(180.0, std::clamp(v, 0.0, 1.0)); }
inline double toneQValue(double q) { return std::clamp(std::log(q / 0.1) / std::log(180.0), 0.0, 1.0); }
constexpr double kToneMaxDb = 18.0;

struct ToneCoef {
   double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// A low pass at the top of the range, or a high pass at the bottom, with no resonance, is left out:
// that's where a new pad's two sit, so they don't touch the sound until they're moved.
inline bool toneOpen(int type, double hz, double q) {
   if (q > 0.72)
      return false;
   if (type == kToneLp12 || type == kToneLp24)
      return hz >= 19900.0;
   if (type == kToneHp12 || type == kToneHp24)
      return hz <= 20.5;
   return false;
}

// A point's biquads (0, 1 or 2 of them) at sample rate fs.
inline int toneDesign(int type, double fs, double hz, double db, double q, ToneCoef out[2]) {
   constexpr double pi = 3.14159265358979323846;
   if (toneOpen(type, hz, q))
      return 0;
   hz = std::clamp(hz, 10.0, 0.45 * fs);
   q = std::max(0.05, q);
   const double w = 2 * pi * hz / fs, c = std::cos(w), sn = std::sin(w);
   const auto set = [](ToneCoef &k, double b0, double b1, double b2, double a0, double a1, double a2) {
      k = {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
   };
   const auto pass = [&](ToneCoef &k, double qq, bool high) {
      const double al = sn / (2 * qq);
      if (high)
         set(k, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
      else
         set(k, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
   };
   const double A = std::pow(10.0, std::clamp(db, -kToneMaxDb, kToneMaxDb) / 40.0);
   switch (type) {
   case kToneBell: {
      const double al = sn / (2 * q);
      set(out[0], 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
      return 1;
   }
   case kToneLowShelf:
   case kToneHighShelf: {
      // The Q sets the shelf's slope: 0.71 is the usual one, higher overshoots a little.
      const double al = sn / (2 * q), sa = 2 * std::sqrt(A) * al;
      if (type == kToneHighShelf)
         set(out[0], A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - sa);
      else
         set(out[0], A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c),
             A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c),
             (A + 1) + (A - 1) * c - sa);
      return 1;
   }
   case kToneLp12:
   case kToneHp12:
      pass(out[0], q, type == kToneHp12);
      return 1;
   case kToneLp24:
   case kToneHp24:
      // Butterworth's 0.54 and 1.31, the second scaled by how far Q is above a plain 0.71.
      pass(out[0], 0.5412, type == kToneHp24);
      pass(out[1], 1.3066 * q / 0.7071, type == kToneHp24);
      return 2;
   case kToneNotch: {
      const double al = sn / (2 * q);
      set(out[0], 1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
      return 1;
   }
   case kToneBandPass: {
      const double al = sn / (2 * q);
      set(out[0], al, 0, -al, 1 + al, -2 * c, 1 - al);
      return 1;
   }
   default:
      return 0;
   }
}

// The gain of `n` biquads at f, in dB.
inline double toneDb(const ToneCoef *k, int n, double fs, double f) {
   constexpr double pi = 3.14159265358979323846;
   const std::complex<double> z1 = std::polar(1.0, -2 * pi * f / fs), z2 = z1 * z1;
   double mag = 1.0;
   for (int i = 0; i < n; ++i) {
      const std::complex<double> num = k[i].b0 + k[i].b1 * z1 + k[i].b2 * z2;
      const std::complex<double> den = 1.0 + k[i].a1 * z1 + k[i].a2 * z2;
      mag *= std::abs(num) / std::max(1e-12, std::abs(den));
   }
   return 20.0 * std::log10(std::max(1e-9, mag));
}

// One point's settings, plain (Hz, dB, Q).
struct TonePoint {
   int type = kToneOff;
   double hz = 1000.0, db = 0.0, q = 0.7071;
};

} // namespace jkit
