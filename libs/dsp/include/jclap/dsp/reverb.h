#pragma once

// Room: a small stereo reverb (JBreath's and JSynth's REVERB). Eight delay lines through a
// Householder mix, each line damped and decaying to the Decay time, with a pre-delay. The mono sum
// goes in; lines feed left and right alternately, added to the dry signal in place. Once the input
// has been silent for longer than the room rings, it empties itself and costs nothing.
//
//    Room room;
//    room.prepare(sampleRate);   // in activate(): allocates the lines
//    room.set(settings);         // whenever a knob moves (cheap when nothing changed)
//    room.tick(l, r);            // per sample, in place

#include <jclap/dsp/math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

namespace jclap::dsp {

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

} // namespace jclap::dsp
