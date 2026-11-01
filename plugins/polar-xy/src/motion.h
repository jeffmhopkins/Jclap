// How Polar XY's output follows the controller: Smoothing glides it, Inertia gives it weight so it
// lags and swings past, and when the controller stops it carries on the way it was going, slowing
// down before it settles. No CLAP dependencies so it can be unit tested.
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
   // How Inertia follows. false (pull): the output is drawn to where the controller is. true (coast):
   // only the controller's movement pushes it, so a controller held still leaves it coasting.
   bool coast = false;

   bool active() const { return smoothing > 0.0 || inertia > 0.0; }
};

// How long inertia (0..1) takes to swing through a cycle. It grows faster than the knob, so the
// low end stays light and full inertia swings for about 5 s, three to four times the weight it had.
inline double inertiaPeriod(double inertia) { return 1.5 * inertia * (1.0 + 2.5 * inertia); }

// How long a coast takes to lose about two thirds of its speed (seconds): 2 s at full inertia.
inline double coastTime(double inertia) { return 0.4 * inertiaPeriod(inertia); }

// How long the controller has to hold still before it counts as stopped and the output coasts on.
// Long enough to bridge the gaps between a moving controller's CCs; after a longer pause, a move
// starts again from rest.
constexpr double kCoastHold = 0.1;

// How much speed a coast keeps when it bounces off the edge of the pad.
constexpr double kCoastBounce = 0.5;

// An angle difference brought into -pi..pi: the short way round.
inline double wrapPi(double a) { return std::remainder(a, 2.0 * kPi); }

// Follows a target position on the -1..+1 pad. In the around space the two axes are an unwrapped
// angle (radians, so a full turn and more is still a turn's worth of distance) and the radius, which
// can go below 0: that's through the centre and out the other side.
//
// With Inertia, the follower tracks how fast the controller is going. When it stops, the target
// carries on at that speed, slowing with friction, and the output keeps chasing it.
class Follower {
public:
   bool placed() const { return placed_; }
   bool moving() const { return moving_; }

   // Jumps straight to (x, y).
   void place(double x, double y) {
      tx_ = rx_ = x;
      ty_ = ry_ = y;
      toSpace(x, y, target_);
      for (int i = 0; i < 2; ++i) {
         real_[i] = smooth_[i] = pos_[i] = target_[i];
         vel_[i] = drift_[i] = 0.0;
      }
      still_ = kCoastHold;
      placed_ = true;
      moving_ = false;
   }

   // A new target to move toward. The first one is jumped to, as there is nowhere to move from.
   void follow(double x, double y) {
      if (!placed_) {
         place(x, y);
         return;
      }
      if (x == rx_ && y == ry_)
         return;
      tx_ = rx_ = x;
      ty_ = ry_ = y;
      double t[2];
      toSpace(x, y, t);
      // The controller's speed, from how far it went since its last move, a little smoothed as the
      // moves arrive unevenly. A move after a pause starts from rest.
      const double a = still_ < kCoastHold ? 1.0 - std::exp(-still_ / 0.05) : 0.0;
      // Coast mode takes a move after a pause as made over the hold, so even a jump gives it a nudge.
      fresh_ = still_ >= kCoastHold;
      for (int i = 0; i < 2; ++i) {
         kick_[i] = fresh_ ? (t[i] - real_[i]) / kCoastHold : 0.0;
         drift_[i] = still_ < kCoastHold ? drift_[i] : 0.0;
         if (still_ > 0.0)
            drift_[i] += a * ((t[i] - real_[i]) / still_ - drift_[i]);
         real_[i] = target_[i] = t[i];
      }
      still_ = 0.0;
      moving_ = true;
   }

   // Stops wherever it is headed (where the controller is, or where a coast would rest).
   void land() {
      if (moving_ && coasting())
         restTarget();
      if (moving_)
         place(tx_, ty_);
   }

   // Switches between straight and around, carrying on from where it is now.
   void setAround(bool around) {
      if (around == around_)
         return;
      double x, y;
      position(x, y);
      const double tx = rx_, ty = ry_;
      const bool wasMoving = moving_;
      around_ = around;
      if (!placed_)
         return;
      place(x, y);
      if (wasMoving)
         follow(tx, ty);
   }

   // Where it is headed, on the pad: the controller, or where a coast has got to.
   void target(double &x, double &y) const {
      x = tx_;
      y = ty_;
   }

   void position(double &x, double &y) const {
      if (!moving_) {
         // Exactly the target, so a settled output is the same as converting the input directly.
         x = tx_;
         y = ty_;
      } else {
         toPad(pos_, x, y);
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
      if (m.coast && m.inertia > 0.0)
         return coastStep(dt, m);
      if (m.inertia > 0.0)
         coast(dt, m);
      else
         drift_[0] = drift_[1] = 0.0;

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
             std::abs(vel_[i]) > eps * 10.0 || std::abs(drift_[i]) > 0.0)
            return true;
      land();
      return false;
   }

private:
   // Coast mode: the output's speed is drawn to the controller's while it moves (slower the heavier it
   // is), and only slows with friction while it holds still. Where the controller is plays no part.
   bool coastStep(double dt, const MotionSettings &m) {
      still_ += dt;
      const bool driven = still_ < kCoastHold;
      const double tau = coastTime(m.inertia);
      const double take = 1.0 - std::exp(-dt * 2.0 * kPi / inertiaPeriod(m.inertia));
      const double drag = std::exp(-dt / tau);
      for (int i = 0; i < 2; ++i) {
         const double speed = fresh_ ? kick_[i] : drift_[i];
         vel_[i] = driven ? vel_[i] + take * (speed - vel_[i]) : vel_[i] * drag;
         pos_[i] += vel_[i] * dt;
      }
      // Through the centre in around, the radius comes out the other side, still a radius.
      if (around_ && pos_[1] < 0.0) {
         pos_[0] += kPi;
         pos_[1] = -pos_[1];
         vel_[1] = -vel_[1];
      }
      for (int i = around_ ? 1 : 0; i < 2; ++i)
         if (std::abs(pos_[i]) > 1.0 && pos_[i] * vel_[i] > 0.0)
            vel_[i] *= -kCoastBounce;
      if (driven || std::abs(vel_[0]) * tau > 1e-3 || std::abs(vel_[1]) * tau > 1e-3)
         return true;
      // At rest: wherever it got to is where it stays, until the controller moves again.
      double x, y;
      toPad(pos_, x, y);
      place(std::clamp(x, -1.0, 1.0), std::clamp(y, -1.0, 1.0));
      return false;
   }

   bool coasting() const { return still_ >= kCoastHold; }

   // Once the controller has stopped, the target drifts on at its speed, slowing with friction and
   // bouncing off the edge of the pad (each axis's in straight, the circle's in around).
   void coast(double dt, const MotionSettings &m) {
      const double before = still_;
      still_ += dt;
      if (!coasting() || (drift_[0] == 0.0 && drift_[1] == 0.0))
         return;
      // Starting out, it catches up on the hold it waited, so the coast follows on from the move.
      const double ran = before < kCoastHold ? still_ : dt;
      const double tau = coastTime(m.inertia);
      const double drag = std::exp(-ran / tau);
      for (int i = 0; i < 2; ++i) {
         target_[i] += drift_[i] * tau * (1.0 - drag);
         drift_[i] *= drag;
      }
      for (int i = around_ ? 1 : 0; i < 2; ++i)
         if (std::abs(target_[i]) > 1.0 && target_[i] * drift_[i] > 0.0)
            drift_[i] *= -kCoastBounce;
      // Stopped once what's left of the coast is well under a CC step.
      if (std::abs(drift_[0]) * tau < 1e-3 && std::abs(drift_[1]) * tau < 1e-3)
         drift_[0] = drift_[1] = 0.0;
      toPad(target_, tx_, ty_);
   }

   // Makes where the coast has got to the place to land: the target, on the pad and inside it.
   void restTarget() {
      if (target_[0] == real_[0] && target_[1] == real_[1])
         return; // never left the controller, so exactly there
      toPad(target_, tx_, ty_);
      tx_ = rx_ = std::clamp(tx_, -1.0, 1.0);
      ty_ = ry_ = std::clamp(ty_, -1.0, 1.0);
   }

   void toPad(const double p[2], double &x, double &y) const {
      if (around_) {
         x = p[1] * std::cos(p[0]);
         y = p[1] * std::sin(p[0]);
      } else {
         x = p[0];
         y = p[1];
      }
   }

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
   double rx_ = 0.0, ry_ = 0.0; // the controller on the pad
   double still_ = 0.0;         // seconds since the controller last moved
   bool fresh_ = false;         // its last move came after a pause: kick_ is its speed, for coast mode
   double kick_[2] = {};
   // The target, the controller (the target's start when it coasts) and its speed, and the output.
   double target_[2] = {}, real_[2] = {}, drift_[2] = {}, smooth_[2] = {}, pos_[2] = {}, vel_[2] = {};
};

} // namespace jclap
