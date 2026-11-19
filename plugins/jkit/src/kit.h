#pragma once

// A kit's sounds, and what the Generate tab does to a generated sound before it goes on a pad.
//
// A Sample is immutable once made and shared (std::shared_ptr<const Sample>): the panel draws it,
// the audio thread plays it, and a pad's sound is changed by swapping in another one. Samples are
// kept at the model's 44.1 kHz and resampled as they play.
//
// They go in the plugin's state, and beside a saved kit, as 16-bit stereo (about 176 kB a second).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "prompts.h"

namespace jkit {

constexpr double kSampleRate = 44100.0;

struct Sample {
   std::string name;   // as the pad shows it: "Snare"
   std::string prompt; // what made it
   int role = kPerc;
   uint32_t seed = 0;
   std::vector<float> left, right;

   size_t frames() const { return left.size(); }
   double seconds() const { return static_cast<double>(frames()) / kSampleRate; }
};
using SamplePtr = std::shared_ptr<const Sample>;

// A pad holds one or more takes of its sound (up to kMaxTakes) and picks one each time it plays.
// Each take has its own trims, level, tune and cuts; the pad's own params (its START and END
// among them, inside the take's) apply on top to whichever take plays.
constexpr int kMaxTakes = 8;
enum TakeMode : int { kTakeRandom, kTakeCycle, kTakeVelocity, kTakeModeCount };
inline const char *const kTakeModeNames[kTakeModeCount] = {"RANDOM", "CYCLE", "VELOCITY"};

struct Take {
   SamplePtr sample;
   double start = 0.0, end = 1.0;      // fractions of the sample
   double fadeInMs = 0.0, fadeOutMs = 0.0;
   double levelDb = 0.0;               // -24 to +12
   double tune = 0.0;                  // semitones, -12 to +12
   double lowCutHz = 20.0;             // 20 is off
   double highCutHz = 20000.0;         // 20000 is off

   // The same take, with every setting in its range (a file can say anything).
   Take clamped() const {
      Take t = *this;
      const auto fin = [](double x, double lo, double hi, double d) { return std::isfinite(x) ? std::clamp(x, lo, hi) : d; };
      t.start = fin(start, 0.0, 1.0, 0.0);
      t.end = fin(end, 0.0, 1.0, 1.0);
      if (t.end < t.start)
         std::swap(t.start, t.end);
      t.fadeInMs = fin(fadeInMs, 0.0, 2000.0, 0.0);
      t.fadeOutMs = fin(fadeOutMs, 0.0, 2000.0, 0.0);
      t.levelDb = fin(levelDb, -24.0, 12.0, 0.0);
      t.tune = fin(tune, -12.0, 12.0, 0.0);
      t.lowCutHz = fin(lowCutHz, 20.0, 20000.0, 20.0);
      t.highCutHz = fin(highCutHz, 20.0, 20000.0, 20000.0);
      return t;
   }
};

struct PadSounds {
   std::vector<Take> takes;
   int mode = kTakeRandom;

   bool empty() const { return takes.empty(); }
   int count() const { return static_cast<int>(takes.size()); }
   // The first take's sound: what the pad shows.
   SamplePtr first() const { return takes.empty() ? nullptr : takes.front().sample; }
   static PadSounds of(SamplePtr s) {
      PadSounds p;
      if (s)
         p.takes.push_back(Take{std::move(s)});
      return p;
   }
};

// Which take a hit plays. RANDOM never plays the same one twice in a row (with two or more);
// CYCLE goes in order; VELOCITY splits 0..1 evenly, the first take the softest. `last` is the take
// played before (or -1), `random` any 32-bit number.
inline int pickTake(const PadSounds &p, int last, float velocity, uint32_t random) {
   const int n = p.count();
   if (n <= 1)
      return 0;
   switch (p.mode) {
      case kTakeCycle:
         return last < 0 ? 0 : (last + 1) % n;
      case kTakeVelocity:
         return std::clamp(static_cast<int>(std::clamp(velocity, 0.0f, 1.0f) * static_cast<float>(n)), 0, n - 1);
      default: {
         if (last < 0 || last >= n)
            return static_cast<int>(random % static_cast<uint32_t>(n));
         const int k = static_cast<int>(random % static_cast<uint32_t>(n - 1));
         return k >= last ? k + 1 : k;
      }
   }
}

// The loudest absolute value in each of `bins` slices of [from, to) (fractions of the sample), for
// drawing a waveform.
inline std::vector<float> overview(const Sample &s, int bins, double from = 0.0, double to = 1.0) {
   std::vector<float> out(static_cast<size_t>(std::max(bins, 0)), 0.0f);
   const size_t n = s.frames();
   if (!n || bins <= 0)
      return out;
   from = std::clamp(from, 0.0, 1.0);
   to = std::clamp(to, from, 1.0);
   for (int b = 0; b < bins; ++b) {
      const size_t a = static_cast<size_t>((from + (to - from) * b / bins) * n);
      const size_t z = std::max(a + 1, static_cast<size_t>((from + (to - from) * (b + 1) / bins) * n));
      float peak = 0.0f;
      for (size_t i = a; i < z && i < n; ++i)
         peak = std::max(peak, std::max(std::fabs(s.left[i]), std::fabs(s.right[i])));
      out[static_cast<size_t>(b)] = peak;
   }
   return out;
}

inline float peakOf(const Sample &s) {
   float peak = 0.0f;
   for (size_t i = 0; i < s.frames(); ++i)
      peak = std::max(peak, std::max(std::fabs(s.left[i]), std::fabs(s.right[i])));
   return peak;
}

// What the SELECTED card does to a sound on its way to a pad.
struct Treatment {
   double start = 0.0, end = 1.0; // fractions of the sound
   double fadeMs = 0.0;           // a fade out at the end
   double fadeInMs = 0.0;         // a fade in at the start
   bool normalize = true;         // peak to -0.3 dBFS
   bool trimSilence = true;       // cut the silence at the start and the tail under -60 dB
   bool reverse = false;
};

// Where the sound starts and stops being louder than -60 dB, as frames.
inline void audibleRange(const Sample &s, size_t &first, size_t &last) {
   const float floor = 0.001f;
   const size_t n = s.frames();
   first = 0;
   last = n;
   while (first < n && std::fabs(s.left[first]) < floor && std::fabs(s.right[first]) < floor)
      ++first;
   while (last > first && std::fabs(s.left[last - 1]) < floor && std::fabs(s.right[last - 1]) < floor)
      --last;
}

// The treated copy of `src`. Never empty while `src` isn't.
inline Sample treat(const Sample &src, const Treatment &t) {
   Sample out = src;
   const size_t n = src.frames();
   if (!n)
      return out;
   size_t a = static_cast<size_t>(std::clamp(t.start, 0.0, 1.0) * n);
   size_t z = static_cast<size_t>(std::clamp(t.end, 0.0, 1.0) * n);
   if (t.trimSilence) {
      size_t first, last;
      audibleRange(src, first, last);
      // A few ms before the first sound, so its attack isn't cut.
      const size_t lead = static_cast<size_t>(0.002 * kSampleRate);
      a = std::max(a, first > lead ? first - lead : 0);
      z = std::min(z, last);
   }
   if (z <= a)
      z = std::min(n, a + 1);
   out.left.assign(src.left.begin() + static_cast<long>(a), src.left.begin() + static_cast<long>(z));
   out.right.assign(src.right.begin() + static_cast<long>(a), src.right.begin() + static_cast<long>(z));
   const size_t m = out.frames();
   // The fade in: the asked-for one (a quarter sine), or 1 ms where it was cut into, so it never
   // starts with a click.
   if (t.fadeInMs >= 1.0) {
      const size_t f = std::min(m, static_cast<size_t>(t.fadeInMs * 0.001 * kSampleRate));
      for (size_t i = 0; i < f; ++i) {
         const float g = static_cast<float>(std::sin(static_cast<double>(i) / static_cast<double>(f) * 1.5707963267948966));
         out.left[i] *= g;
         out.right[i] *= g;
      }
   } else if (a > 0) {
      const size_t f = std::min(m, static_cast<size_t>(0.001 * kSampleRate));
      for (size_t i = 0; i < f; ++i) {
         const float g = static_cast<float>(i) / static_cast<float>(f);
         out.left[i] *= g;
         out.right[i] *= g;
      }
   }
   // The fade out: the asked-for one, or 2 ms where the end was cut.
   double fadeMs = t.fadeMs;
   if (fadeMs < 2.0 && z < n)
      fadeMs = 2.0;
   const size_t fade = std::min(m, static_cast<size_t>(fadeMs * 0.001 * kSampleRate));
   for (size_t i = 0; i < fade; ++i) {
      const double x = static_cast<double>(i + 1) / static_cast<double>(fade); // 0 -> 1 toward the end
      const float g = static_cast<float>(std::cos(x * 1.5707963267948966));
      out.left[m - fade + i] *= g;
      out.right[m - fade + i] *= g;
   }
   if (t.reverse) {
      std::reverse(out.left.begin(), out.left.end());
      std::reverse(out.right.begin(), out.right.end());
   }
   if (t.normalize) {
      const float peak = peakOf(out);
      if (peak > 1e-5f) {
         const float g = 0.9661f / peak; // -0.3 dBFS
         for (size_t i = 0; i < m; ++i) {
            out.left[i] *= g;
            out.right[i] *= g;
         }
      }
   }
   return out;
}

// ---- reading and writing ----

class Writer {
public:
   std::string bytes;
   void u8(uint8_t v) { bytes.push_back(static_cast<char>(v)); }
   void u32(uint32_t v) {
      for (int i = 0; i < 4; ++i)
         u8(static_cast<uint8_t>(v >> (8 * i)));
   }
   void f64(double v) {
      uint64_t u;
      std::memcpy(&u, &v, 8);
      for (int i = 0; i < 8; ++i)
         u8(static_cast<uint8_t>(u >> (8 * i)));
   }
   void str(const std::string &s) {
      u32(static_cast<uint32_t>(s.size()));
      bytes += s;
   }
   void sample(const Sample &s) {
      str(s.name);
      str(s.prompt);
      u32(static_cast<uint32_t>(s.role));
      u32(s.seed);
      u32(static_cast<uint32_t>(s.frames()));
      for (size_t i = 0; i < s.frames(); ++i)
         for (float x : {s.left[i], s.right[i]}) {
            const int v = static_cast<int>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
            u8(static_cast<uint8_t>(v & 0xFF));
            u8(static_cast<uint8_t>((v >> 8) & 0xFF));
         }
   }
};

class Reader {
public:
   Reader(const char *data, size_t size) : p_(reinterpret_cast<const uint8_t *>(data)), end_(p_ + size) {}
   bool ok() const { return ok_; }
   size_t left() const { return static_cast<size_t>(end_ - p_); }
   uint8_t u8() {
      if (p_ >= end_) {
         ok_ = false;
         return 0;
      }
      return *p_++;
   }
   uint32_t u32() {
      uint32_t v = 0;
      for (int i = 0; i < 4; ++i)
         v |= static_cast<uint32_t>(u8()) << (8 * i);
      return v;
   }
   double f64() {
      uint64_t u = 0;
      for (int i = 0; i < 8; ++i)
         u |= static_cast<uint64_t>(u8()) << (8 * i);
      double v;
      std::memcpy(&v, &u, 8);
      return v;
   }
   std::string str() {
      const uint32_t n = u32();
      if (!ok_ || n > left()) {
         ok_ = false;
         return {};
      }
      std::string s(reinterpret_cast<const char *>(p_), n);
      p_ += n;
      return s;
   }
   bool sample(Sample &s) {
      s.name = str();
      s.prompt = str();
      s.role = static_cast<int>(u32());
      s.seed = u32();
      const uint32_t frames = u32();
      if (!ok_ || static_cast<uint64_t>(frames) * 4 > left() || s.role < 0 || s.role >= kRoleCount)
         return ok_ = false;
      s.left.resize(frames);
      s.right.resize(frames);
      for (uint32_t i = 0; i < frames; ++i) {
         for (float *x : {&s.left[i], &s.right[i]}) {
            const uint16_t lo = p_[0], hi = p_[1];
            p_ += 2;
            *x = static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(lo | hi << 8))) / 32767.0f;
         }
      }
      return true;
   }

private:
   const uint8_t *p_, *end_;
   bool ok_ = true;
};

// A kit's sounds: "JKS2", then for each pad its number of takes (0 for empty) and its mode, and
// each take's sample and settings. "JKS1" (before 0.7: a 0 or 1 and one sample a pad) still reads.
inline std::string writeKit(const PadSounds (&pads)[16]) {
   Writer w;
   w.bytes = "JKS2";
   for (const PadSounds &p : pads) {
      int n = 0;
      for (const Take &t : p.takes)
         n += t.sample && n < kMaxTakes ? 1 : 0;
      w.u8(static_cast<uint8_t>(n));
      w.u8(static_cast<uint8_t>(p.mode));
      int written = 0;
      for (const Take &t : p.takes) {
         if (!t.sample || written >= kMaxTakes)
            continue;
         ++written;
         w.sample(*t.sample);
         for (double x : {t.start, t.end, t.fadeInMs, t.fadeOutMs, t.levelDb, t.tune, t.lowCutHz, t.highCutHz})
            w.f64(x);
      }
   }
   return w.bytes;
}

inline bool readKit(const std::string &bytes, PadSounds (&pads)[16]) {
   if (bytes.size() < 4)
      return false;
   const bool v1 = bytes.compare(0, 4, "JKS1") == 0;
   if (!v1 && bytes.compare(0, 4, "JKS2") != 0)
      return false;
   Reader r(bytes.data() + 4, bytes.size() - 4);
   PadSounds read[16];
   for (PadSounds &p : read) {
      const int n = r.u8();
      if (!v1)
         p.mode = std::clamp(static_cast<int>(r.u8()), 0, kTakeModeCount - 1);
      if (n > (v1 ? 1 : kMaxTakes))
         return false;
      for (int i = 0; i < n; ++i) {
         auto sample = std::make_shared<Sample>();
         if (!r.sample(*sample))
            return false;
         Take t;
         t.sample = std::move(sample);
         if (!v1) {
            t.start = r.f64();
            t.end = r.f64();
            t.fadeInMs = r.f64();
            t.fadeOutMs = r.f64();
            t.levelDb = r.f64();
            t.tune = r.f64();
            t.lowCutHz = r.f64();
            t.highCutHz = r.f64();
         }
         p.takes.push_back(t.clamped());
      }
      if (!r.ok())
         return false;
   }
   for (int i = 0; i < 16; ++i)
      pads[i] = std::move(read[i]);
   return true;
}

} // namespace jkit
