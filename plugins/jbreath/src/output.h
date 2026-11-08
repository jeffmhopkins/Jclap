#pragma once

// What happens to the voices' sum on its way out, at the host's rate, per channel: the instrument's
// body (its radiation, barrel, walls, the wood or metal it's made of, and the length of tube below
// the open holes), then a room and a limiter on the stereo pair.

#include <algorithm>
#include <cmath>
#include <vector>

#include <jclap/dsp/math.h>

#include "models.h"
#include "params.h"

namespace jbreath {

// The body's settings, from the Bore's knobs (params.h).
// cutoffHz is the Hole Cutoff knob, which the lattice's radiation follows as the bore's does.
struct BodySettings {
   double radiate = 0.66, barrel = 0.4, wall = 0.5, length = 0.65, cutoffHz = 1300.0;
   int holes = 3, tube = 0;
   bool operator==(const BodySettings &o) const {
      return radiate == o.radiate && barrel == o.barrel && wall == o.wall && length == o.length && holes == o.holes &&
             tube == o.tube && cutoffHz == o.cutoffHz;
   }
};

// One channel of the body. Every stage is a fixed filter, so the voices share it. At the defaults
// (Lattice 7, grenadilla, Wall 50%, Radiate 66%) it leaves each model's own radiated spectrum alone
// but for the barrel's small cavity resonance: the models are tuned against recordings of the real
// instruments, and the alternatives move away from that.
class Body {
public:
   void prepare(double fs) {
      fs_ = fs;
      comb_.assign(static_cast<size_t>(fs * 0.012) + 4, 0.0f);
      reset();
      set(settings_, true);
   }
   void reset() {
      for (Biquad *b : {&radiate_, &barrel_, &wallTilt_, &wall_, &lattice_, &low_, &tone_, &tone2_, &high_})
         b->clear();
      std::fill(comb_.begin(), comb_.end(), 0.0f);
      pos_ = 0;
      damp_ = hp_ = 0.0f;
   }
   void set(const BodySettings &s, bool force = false) {
      if (s == settings_ && !force)
         return;
      settings_ = s;
      // The open end and holes radiate the highs better than the lows (the radiation resistance
      // grows as (ka)^2 until ka ~ 1): a wider bell or bigger holes, up to +7 dB above 1.5 kHz; a
      // narrow end, as much as 13 dB less.
      radiate_.highshelf(1500.0, (s.radiate - 0.66) * 20.0, fs_);
      // The barrel's (or mouthpiece's) cavity: a resonance, lower and stronger the bigger it is.
      barrel_.peak(3200.0 * std::pow(2.0, -2.0 * s.barrel), 1.2, 1.0 + 6.25 * s.barrel, fs_);
      // Walls soak up the highs: the boundary layer's losses grow with frequency, more on a soft,
      // porous wall (bone, cane) than a hard one (metal): a tilt from +3 dB to -8 dB above 1.2 kHz,
      // and the very top rolled off.
      wallTilt_.highshelf(1200.0, s.wall < 0.5 ? 6.0 * (0.5 - s.wall) : -16.0 * (s.wall - 0.5), fs_);
      wall_.lowpass(std::min(0.45 * fs_, 18000.0 * std::pow(2.0, -2.5 * (s.wall - 0.2))), 0.6, fs_);
      // HOLES, outside the bore: above the lattice's cutoff the sound leaves through the open holes,
      // so the more of them are open, the more highs are radiated; with few open (or all closed
      // below the note) the highs stay in the tube and the spectrum falls off faster above the
      // cutoff. A shelf at the lattice's cutoff (scaled as in the bore, applyCharacter), and a
      // little make-up gain, so a darker setting isn't only a quieter one.
      static const float holeK[] = {0.5f, 0.64f, 0.8f, 1.0f, 1.22f, 1.5f};
      static const float holeDb[] = {-8.0f, -5.0f, -2.5f, 0.0f, 1.5f, 3.0f};
      static const float makeup[] = {4.0f, 2.5f, 1.0f, 0.0f, -0.8f, -1.5f};
      const int h = std::clamp(s.holes, 0, 5);
      const double knob = std::clamp(s.cutoffHz / 1300.0, 0.3, 3.5);
      const double fc = std::min(1800.0 * holeK[h] * knob, 0.4 * fs_);
      lattice_.highshelf(fc, holeDb[h], fs_);
      gain_ = static_cast<float>(std::pow(10.0, makeup[h] / 20.0));
      // The tube below the open holes: only what passes the lattice (above its cutoff) gets down
      // there, and it comes back off the closed holes and the bell as weak resonances spaced by the
      // tube's length. Closed holes below reflect it all (the veiled, cross-fingered colour); each
      // open hole lets some of it out on the way; all open, there's no tube left below.
      static const float feedback[] = {0.6f, 0.48f, 0.34f, 0.2f, 0.12f, 0.0f};
      fb_ = feedback[h];
      hpA_ = static_cast<float>(std::exp(-2 * kPi * std::min(1300.0 * holeK[h] * knob, 0.4 * fs_) / fs_));
      delay_ = std::clamp(2.0 * bodyLength(s.length) / 343.0 * fs_, 2.0, comb_.size() - 2.0);
      dampA_ = static_cast<float>(std::exp(-2 * kPi * std::min(5000.0, 0.4 * fs_) / fs_));
      // TUBE, outside the bore (its inner surface's losses are in the bore: applyCharacter). The
      // material itself hardly changes the sound (Backus 1964 on clarinets, Coltman 1971 and
      // Widholm 2001 on flutes): what's physical here is that thin metal walls vibrate a little,
      // adding narrow ringing resonances (Kausel et al. 2010, on brass), and thick wood and glass
      // don't. The rest is a stylised colour, how each material is usually heard: wood warm, metal
      // and glass bright. Grenadilla is the neutral reference.
      struct Material {
         double low, hz1, q1, db1, hz2, q2, db2, high;
      };
      static const Material materials[] = {
         {0.0, 1000, 1.0, 0.0, 3000, 1.0, 0.0, 0.0},  // grenadilla: as tuned
         {2.5, 700, 0.9, 2.0, 3000, 1.0, 0.0, -3.5},  // boxwood: warm, round
         {3.5, 450, 0.8, 2.5, 2500, 1.0, -3.0, -5.5}, // maple: soft, woody, dull top
         {-1.0, 1100, 1.4, -4.0, 2700, 2.0, 5.0, 1.0},// bamboo: hollow mids, a breathy edge
         {-1.5, 1150, 6.0, 3.0, 2900, 5.0, 4.0, 3.0}, // brass: wall modes, bright and ringing
         {-1.5, 2300, 5.0, 2.5, 4400, 3.0, 3.5, 3.5}, // silver: thin wall, sweet airy top
         {0.0, 1500, 0.8, -2.5, 3000, 1.0, 0.0, -2.0},// resin: plain, a little dull
         {-3.0, 3800, 6.0, 4.0, 6200, 4.0, 5.0, 4.5}, // glass: thin and glassy
      };
      const Material &m = materials[std::clamp(s.tube, 0, 7)];
      low_.lowshelf(250.0, m.low, fs_);
      tone_.peak(std::min(m.hz1, 0.4 * fs_), m.q1, m.db1, fs_);
      tone2_.peak(std::min(m.hz2, 0.4 * fs_), m.q2, m.db2, fs_);
      high_.highshelf(std::min(3500.0, 0.4 * fs_), m.high, fs_);
   }

   float tick(float x) {
      // The tube below the holes: what passes the lattice, echoing back at its length, damped.
      if (fb_ > 0.0f) {
         hp_ = (1 - hpA_) * x + hpA_ * hp_;
         const float above = x - hp_;
         const size_t n = comb_.size();
         double r = pos_ + n - delay_;
         const size_t i = static_cast<size_t>(r) % n;
         const float f = static_cast<float>(r - std::floor(r));
         const float back = comb_[i] + f * (comb_[(i + 1) % n] - comb_[i]);
         damp_ = (1 - dampA_) * back + dampA_ * damp_;
         comb_[pos_] = above + fb_ * damp_;
         pos_ = (pos_ + 1) % n;
         x += 1.8f * fb_ * damp_;
      }
      x = lattice_.tick(x) * gain_;
      return high_.tick(tone2_.tick(tone_.tick(low_.tick(wall_.tick(wallTilt_.tick(barrel_.tick(radiate_.tick(x))))))));
   }

private:
   BodySettings settings_;
   Biquad radiate_, barrel_, wallTilt_, wall_, lattice_, low_, tone_, tone2_, high_;
   std::vector<float> comb_;
   size_t pos_ = 0;
   double delay_ = 100.0, fs_ = 48000.0;
   float fb_ = 0.0f, damp_ = 0.0f, dampA_ = 0.5f, hp_ = 0.0f, hpA_ = 0.5f, gain_ = 1.0f;
};

// A small room: eight delay lines through a Householder mix, each line damped and decaying to the
// Decay time, with a pre-delay. Lines feed left and right alternately.
struct RoomSettings {
   double mix = 0.15, size = 0.5, decay = 1.6, damp = 0.5, preDelayMs = 12.0;
   bool operator==(const RoomSettings &o) const {
      return mix == o.mix && size == o.size && decay == o.decay && damp == o.damp && preDelayMs == o.preDelayMs;
   }
};

class Room {
public:
   static constexpr int kLines = 8;
   void prepare(double fs) {
      fs_ = fs;
      for (auto &l : lines_)
         l.assign(static_cast<size_t>(fs * 0.16) + 8, 0.0f);
      pre_.assign(static_cast<size_t>(fs * 0.11) + 8, 0.0f);
      reset();
      set(settings_, true);
   }
   void reset() {
      for (auto &l : lines_)
         std::fill(l.begin(), l.end(), 0.0f);
      std::fill(pre_.begin(), pre_.end(), 0.0f);
      std::fill(std::begin(lp_), std::end(lp_), 0.0f);
      pos_ = prePos_ = 0;
      quiet_ = 0;
   }
   void set(const RoomSettings &s, bool force = false) {
      if (s == settings_ && !force)
         return;
      settings_ = s;
      // Mutually prime lengths in ms, scaled from a closet to a hall.
      static const double base[kLines] = {29.7, 37.1, 41.1, 43.7, 53.0, 59.3, 67.1, 73.3};
      const double scale = 0.35 + 1.6 * s.size;
      for (int i = 0; i < kLines; ++i) {
         len_[i] = std::clamp(static_cast<size_t>(base[i] * scale * fs_ / 1000.0), size_t{8}, lines_[i].size() - 1);
         gain_[i] = static_cast<float>(std::pow(10.0, -3.0 * len_[i] / (fs_ * std::max(0.1, s.decay))));
      }
      dampA_ = static_cast<float>(std::exp(-2 * kPi * (12000.0 * std::pow(2.0, -4.0 * s.damp)) / fs_));
      preLen_ = std::clamp(static_cast<size_t>(s.preDelayMs * fs_ / 1000.0), size_t{1}, pre_.size() - 1);
      wet_ = static_cast<float>(s.mix);
      limit_ = static_cast<int>(fs_ * (s.decay + 0.3) + preLen_);
   }
   bool active() const { return wet_ > 0.0f; }

   // In place on a stereo pair.
   void tick(float &l, float &r) {
      if (wet_ <= 0.0f)
         return;
      const float in = 0.5f * (l + r);
      // Silent in for longer than the room rings: empty it, once, and stay quiet.
      if (in == 0.0f) {
         if (quiet_ > limit_)
            return;
         if (++quiet_ > limit_) {
            for (auto &line : lines_)
               std::fill(line.begin(), line.end(), 0.0f);
            std::fill(std::begin(lp_), std::end(lp_), 0.0f);
            return;
         }
      } else {
         quiet_ = 0;
      }
      pre_[prePos_] = in;
      const size_t pr = (prePos_ + pre_.size() - preLen_) % pre_.size();
      prePos_ = (prePos_ + 1) % pre_.size();
      const float x = pre_[pr];
      float out[kLines], sum = 0.0f;
      for (int i = 0; i < kLines; ++i) {
         const auto &line = lines_[i];
         const size_t rd = (pos_ + line.size() - len_[i]) % line.size();
         lp_[i] = (1 - dampA_) * line[rd] + dampA_ * lp_[i];
         out[i] = lp_[i] * gain_[i];
         sum += out[i];
      }
      const float h = sum * (2.0f / kLines);
      float wl = 0.0f, wr = 0.0f;
      for (int i = 0; i < kLines; ++i) {
         lines_[i][pos_ % lines_[i].size()] = out[i] - h + x * 0.35f;
         (i & 1 ? wr : wl) += out[i];
      }
      pos_ = (pos_ + 1) % lines_[0].size();
      l += wet_ * 0.5f * wl;
      r += wet_ * 0.5f * wr;
   }

private:
   RoomSettings settings_;
   std::vector<float> lines_[kLines], pre_;
   size_t len_[kLines] = {}, pos_ = 0, prePos_ = 0, preLen_ = 1;
   float gain_[kLines] = {}, lp_[kLines] = {};
   float dampA_ = 0.5f, wet_ = 0.0f;
   int quiet_ = 0, limit_ = 1 << 30;
   double fs_ = 48000.0;
};

// The last stage: none, the soft clip JBreath always had, or a peak limiter just under full scale
// (with the soft clip after it for what the limiter is too slow to catch).
enum LimiterMode { kLimitOff, kLimitSoft, kLimitHard };
class Limiter {
public:
   void prepare(double fs) {
      release_ = static_cast<float>(std::exp(-1.0 / (0.08 * fs)));
      reset();
   }
   void reset() { env_ = 0.0f; }
   void tick(float &l, float &r, int mode) {
      if (mode == kLimitOff)
         return;
      if (mode == kLimitHard) {
         const float peak = std::max(std::fabs(l), std::fabs(r));
         env_ = peak > env_ ? peak : env_ * release_ + (1 - release_) * peak;
         const float g = env_ > 0.9f ? 0.9f / env_ : 1.0f;
         l *= g;
         r *= g;
      }
      l = soft(l);
      r = soft(r);
   }

private:
   static float soft(float x) { return jclap::dsp::softClip(x); }
   float env_ = 0.0f, release_ = 0.999f;
};

} // namespace jbreath
