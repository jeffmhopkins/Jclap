#pragma once

// The orbit simulation: two to six bodies under their mutual gravity and nothing else, so the motion
// braids, swaps partners and slingshots without ever settling. The audio thread steps it every
// control tick; it never allocates, and the same steps from the same start give the same orbits.
//
// Units: G = 1 times the Gravity param, masses are the Mass params (about 1), and the default orbits
// span about two units. The centre of mass is kept at the origin and at rest, so the system never
// drifts off.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jverb {

class OrbitSim {
public:
   static constexpr int kMax = 6;

   struct Body {
      double x = 0, y = 0, vx = 0, vy = 0;
   };

   // Puts `count` bodies at their start positions: three start on a figure-eight (stable with
   // equal masses, chaotic within a few laps with unequal ones), any other count on a ring at its circular speed. `velocity` scales each
   // body's start velocity.
   void reset(int count, const double *mass, const double *velocity, double gravity) {
      count_ = std::clamp(count, 2, kMax);
      for (Body &b : bodies_)
         b = Body{};
      if (count_ == 3) {
         static const double kStart[3][4] = {
            {0.97066981113, -0.2432542951, 0.4817968358, 0.4626006981},
            {-0.96933890887, 0.2429207649, 0.5147853882, 0.4776828297},
            {0.00066545112736, -0.00016676509669, -0.9036966175, -0.8526297113},
         };
         for (int i = 0; i < 3; ++i)
            bodies_[i] = {kStart[i][0], kStart[i][1], kStart[i][2], kStart[i][3]};
      }
      else {
         // Equal masses on a ring: each feels sum(1 / (4 sin(pi k / n))) times m / r^2 inward.
         double s = 0.0;
         for (int k = 1; k < count_; ++k)
            s += 0.25 / std::sin(3.14159265358979 * k / count_);
         const double r = 1.0, v = std::sqrt(s / r);
         for (int i = 0; i < count_; ++i) {
            const double a = 2.0 * 3.14159265358979 * i / count_ + 0.3;
            // A slightly different radius each, so the ring breaks up into something interesting.
            const double ri = r * (1.0 + 0.06 * ((i * 7) % 5 - 2) / 2.0);
            bodies_[i] = {ri * std::cos(a), ri * std::sin(a), -v * std::sin(a), v * std::cos(a)};
         }
      }
      // Speeds scale with sqrt(G) so a stronger gravity gives the same shapes, only faster.
      const double g = std::sqrt(std::max(0.01, gravity));
      for (int i = 0; i < count_; ++i) {
         bodies_[i].vx *= velocity[i] * g;
         bodies_[i].vy *= velocity[i] * g;
      }
      recentre(mass);
   }

   int count() const { return count_; }
   const Body &body(int i) const { return bodies_[i]; }

   // Adds body `count()` at (x, y), moving at the speed of a circular orbit around everything else.
   void add(double x, double y, const double *mass, double gravity) {
      if (count_ >= kMax)
         return;
      double total = 0.0;
      for (int i = 0; i < count_; ++i)
         total += mass[i];
      const double r = std::max(0.2, std::hypot(x, y));
      const double v = std::sqrt(std::max(0.01, gravity) * total / r) * 0.85;
      bodies_[count_] = {x, y, -v * y / r, v * x / r};
      ++count_;
      recentre(mass);
   }
   // Where a new body goes when nobody said: outside the others, on the far side of the newest one.
   void spawnPoint(double &x, double &y) const {
      double extent = 0.5;
      for (int i = 0; i < count_; ++i)
         extent = std::max(extent, std::hypot(bodies_[i].x, bodies_[i].y));
      const Body &last = bodies_[count_ - 1];
      const double a = std::atan2(last.y, last.x) + 3.14159265358979 * 0.8;
      x = 1.2 * extent * std::cos(a);
      y = 1.2 * extent * std::sin(a);
   }
   void remove() { count_ = std::max(2, count_ - 1); }

   void place(int i, double x, double y) {
      if (i < count_)
         bodies_[i].x = x, bodies_[i].y = y;
   }
   void setVelocity(int i, double vx, double vy) {
      if (i < count_)
         bodies_[i].vx = vx, bodies_[i].vy = vy;
   }
   void kick(int i, double dvx, double dvy) {
      if (i < count_)
         bodies_[i].vx += dvx, bodies_[i].vy += dvy;
   }
   void scaleVelocity(int i, double ratio) {
      if (i < count_)
         bodies_[i].vx *= ratio, bodies_[i].vy *= ratio;
   }

   // Advances `dt` time units. Close passes take smaller steps so slingshots stay accurate.
   void step(double dt, const double *mass, double gravity) {
      double minD2 = 1e9;
      for (int i = 0; i < count_; ++i)
         for (int j = i + 1; j < count_; ++j) {
            const double dx = bodies_[j].x - bodies_[i].x, dy = bodies_[j].y - bodies_[i].y;
            minD2 = std::min(minD2, dx * dx + dy * dy);
         }
      const double minD = std::sqrt(minD2) + kSoft;
      const double h = 0.01 * std::min(1.0, minD * std::sqrt(minD)) / std::sqrt(std::max(0.25, gravity));
      const int n = std::clamp(static_cast<int>(std::ceil(dt / h)), 1, 64);
      const double sub = dt / n;
      for (int s = 0; s < n; ++s) {
         accelerations(mass, gravity);
         for (int i = 0; i < count_; ++i) {
            bodies_[i].vx += 0.5 * sub * ax_[i];
            bodies_[i].vy += 0.5 * sub * ay_[i];
            bodies_[i].x += sub * bodies_[i].vx;
            bodies_[i].y += sub * bodies_[i].vy;
         }
         accelerations(mass, gravity);
         for (int i = 0; i < count_; ++i) {
            bodies_[i].vx += 0.5 * sub * ax_[i];
            bodies_[i].vy += 0.5 * sub * ay_[i];
         }
      }
      recentre(mass);
   }

   // Distance from the centre of mass, and from the nearest other body.
   bool finite() const {
      for (int i = 0; i < count_; ++i) {
         const Body &b = bodies_[i];
         if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.vx) || !std::isfinite(b.vy))
            return false;
      }
      return true;
   }
   double radius(int i) const { return std::hypot(bodies_[i].x, bodies_[i].y); }
   double speed(int i) const { return std::hypot(bodies_[i].vx, bodies_[i].vy); }
   double nearest(int i) const {
      double d = 1e9;
      for (int j = 0; j < count_; ++j)
         if (j != i)
            d = std::min(d, std::hypot(bodies_[j].x - bodies_[i].x, bodies_[j].y - bodies_[i].y));
      return d;
   }

private:
   // Softening keeps close passes finite; the tether pulls back bodies flung past kTether.
   static constexpr double kSoft = 0.05, kTether = 2.4;

   void accelerations(const double *mass, double gravity) {
      for (int i = 0; i < count_; ++i) {
         double ax = 0.0, ay = 0.0;
         for (int j = 0; j < count_; ++j) {
            if (j == i)
               continue;
            const double dx = bodies_[j].x - bodies_[i].x, dy = bodies_[j].y - bodies_[i].y;
            const double r2 = dx * dx + dy * dy + kSoft * kSoft;
            const double f = gravity * mass[j] / (r2 * std::sqrt(r2));
            ax += dx * f;
            ay += dy * f;
         }
         const double r = std::hypot(bodies_[i].x, bodies_[i].y);
         if (r > kTether) {
            const double pull = 3.0 * (r - kTether) / r;
            ax -= bodies_[i].x * pull;
            ay -= bodies_[i].y * pull;
            // And a little drag out there, so an escape is caught and turned around, not bounced.
            ax -= 0.4 * bodies_[i].vx * (r - kTether);
            ay -= 0.4 * bodies_[i].vy * (r - kTether);
         }
         ax_[i] = ax;
         ay_[i] = ay;
      }
   }

   void recentre(const double *mass) {
      double m = 0, x = 0, y = 0, vx = 0, vy = 0;
      for (int i = 0; i < count_; ++i) {
         m += mass[i];
         x += mass[i] * bodies_[i].x;
         y += mass[i] * bodies_[i].y;
         vx += mass[i] * bodies_[i].vx;
         vy += mass[i] * bodies_[i].vy;
      }
      if (m <= 0.0)
         return;
      for (int i = 0; i < count_; ++i) {
         bodies_[i].x -= x / m;
         bodies_[i].y -= y / m;
         bodies_[i].vx -= vx / m;
         bodies_[i].vy -= vy / m;
      }
   }

   Body bodies_[kMax];
   double ax_[kMax] = {}, ay_[kMax] = {};
   int count_ = 3;
};

} // namespace jverb
