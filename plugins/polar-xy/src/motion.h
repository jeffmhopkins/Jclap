// How Polar XY's output follows the controller: Smoothing glides it, Inertia gives it weight so it
// lags, swings past and settles. No CLAP dependencies so it can be unit tested.
#pragma once

#include <algorithm>
#include <cmath>

#include "polar.h"

namespace jclap {

struct MotionSettings {
   double smoothing = 0.0; // seconds to cover about 95% of a move; 0 = none
   double inertia = 0.0;   // 0..1; 0 = no weight
   // false: moves in a straight line across the pad. true: angle and radius move on their own, so
   // the output goes around the circle and keeps its spin (a whirl carries on past where it stopped).
   bool around = false;

   bool active() const { return smoothing > 0.0 || inertia > 0.0; }
};

// How long inertia (0..1) takes to swing through a cycle. It grows faster than the knob, so the
// low end stays light and full inertia swings for about 5 s, three to four times the weight it had.
inline double inertiaPeriod(double inertia) { return 1.5 * inertia * (1.0 + 2.5 * inertia); }

// An angle difference brought into -pi..pi: the short way round.
inline double wrapPi(double a) { return std::remainder(a, 2.0 * kPi); }

// Follows a target position on the -1..+1 pad. In the around space the two axes are an unwrapped
// angle (radians, so a full turn and more is still a turn's worth of distance) and the radius.
class Follower {
public:
   bool placed() const { return placed_; }
   bool moving() const { return moving_; }

   // Jumps straight to (x, y).
   void place(double x, double y) {
      tx_ = x;
      ty_ = y;
      toSpace(x, y, target_);
      for (int i = 0; i < 2; ++i) {
         smooth_[i] = pos_[i] = target_[i];
         vel_[i] = 0.0;
      }
      placed_ = true;
      moving_ = false;
   }

   // A new target to move toward. The first one is jumped to, as there is nowhere to move from.
   void follow(double x, double y) {
      if (!placed_) {
         place(x, y);
         return;
      }
      if (!moving_ && x == tx_ && y == ty_)
         return;
      tx_ = x;
      ty_ = y;
      toSpace(x, y, target_);
      moving_ = true;
   }

   // Stops wherever it is headed.
   void land() {
      if (moving_)
         place(tx_, ty_);
   }

   // Switches between straight and around, carrying on from where it is now.
   void setAround(bool around) {
      if (around == around_)
         return;
      double x, y;
      position(x, y);
      const double tx = tx_, ty = ty_;
      const bool wasMoving = moving_;
      around_ = around;
      if (!placed_)
         return;
      place(x, y);
      if (wasMoving)
         follow(tx, ty);
   }

   // Where it is headed: the last target, on the pad.
   void target(double &x, double &y) const {
      x = tx_;
      y = ty_;
   }

   void position(double &x, double &y) const {
      if (!moving_) {
         // Exactly the target, so a settled output is the same as converting the input directly.
         x = tx_;
         y = ty_;
      } else if (around_) {
         const double r = std::max(pos_[1], 0.0);
         x = r * std::cos(pos_[0]);
         y = r * std::sin(pos_[0]);
      } else {
         x = pos_[0];
         y = pos_[1];
      }
   }

   // Moves dt seconds on. Returns whether it is still moving.
   bool step(double dt, const MotionSettings &m) {
      if (!moving_)
         return false;
      if (!m.active()) {
         land();
         return false;
      }
      // Smoothing: a one-pole glide, about 95% of the way in m.smoothing (three time constants).
      const double a = m.smoothing > 0.0 ? 1.0 - std::exp(-dt * 3.0 / m.smoothing) : 1.0;
      for (int i = 0; i < 2; ++i)
         smooth_[i] += a * (target_[i] - smooth_[i]);

      // Inertia: a mass on a spring pulled toward the smoothed position. Heavier swings slower and
      // with less damping, so it overshoots further.
      if (m.inertia > 0.0) {
         const double w = 2.0 * kPi / inertiaPeriod(m.inertia);
         const double zeta = 0.85 - 0.7 * m.inertia;
         const int n = std::max(1, static_cast<int>(std::ceil(w * dt / 0.05)));
         const double h = dt / n;
         for (int k = 0; k < n; ++k)
            for (int i = 0; i < 2; ++i) {
               vel_[i] += h * (w * w * (smooth_[i] - pos_[i]) - 2.0 * zeta * w * vel_[i]);
               pos_[i] += h * vel_[i];
            }
      } else {
         for (int i = 0; i < 2; ++i) {
            pos_[i] = smooth_[i];
            vel_[i] = 0.0;
         }
      }

      constexpr double eps = 1e-4;
      for (int i = 0; i < 2; ++i)
         if (std::abs(target_[i] - pos_[i]) > eps || std::abs(target_[i] - smooth_[i]) > eps ||
             std::abs(vel_[i]) > eps * 10.0)
            return true;
      land();
      return false;
   }

private:
   void toSpace(double x, double y, double out[2]) const {
      if (!around_) {
         out[0] = x;
         out[1] = y;
         return;
      }
      // The angle unwraps from the current target's, so it takes the short way round and whirls
      // add up. Inside the dead zone the angle is undefined and holds.
      const double r = std::hypot(x, y);
      const double ref = placed_ ? target_[0] : 0.0;
      out[0] = r < kDeadZone ? ref : ref + wrapPi(std::atan2(y, x) - ref);
      out[1] = r;
   }

   bool around_ = false, placed_ = false, moving_ = false;
   double tx_ = 0.0, ty_ = 0.0; // the target on the pad
   double target_[2] = {}, smooth_[2] = {}, pos_[2] = {}, vel_[2] = {};
};

} // namespace jclap
