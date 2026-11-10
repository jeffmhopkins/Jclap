#pragma once

// The orbit simulation: two to six bodies under their mutual gravity, so the motion braids, swaps
// partners and slingshots without ever settling. The audio thread steps it every
// control tick; it never allocates, and the same steps from the same start give the same orbits.
//
// Units: G = 1 times the Gravity param, masses are the Mass params (about 1), and the default orbits
// span about two units. The centre of mass is kept at the origin and at rest, so the system never
// drifts off.
//
// Left to plain gravity, bodies pair off: a close pass hands energy from one body to the others, the
// pair it leaves behind sinks deeper, and with four or more bodies those pairs keep hardening until
// they sit on top of each other for good. Three things stop that, none of them a wall that bodies
// bounce off. Gravity is softened over kSoft, so a close pass bends smoothly (or slips straight
// through) instead of whipping or reflecting. A pair bound tighter than an orbit of kPairSize is
// eased loose over a second or so, paid for by the other bodies. And the system's energy is held at
// what it started with (what the start, a drag, a throw or the Velocity params gave it), so neither
// the tether nor transient kicks can drain or pump it over time.

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
      rebase(mass, gravity);
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
      rebase(mass, gravity);
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
   // Removes body `i` (the last if out of range), moving the ones after it down a place.
   void remove(int i = -1) {
      if (count_ <= 2)
         return;
      if (i < 0 || i >= count_)
         i = count_ - 1;
      for (int k = i; k + 1 < count_; ++k)
         bodies_[k] = bodies_[k + 1];
      bodies_[--count_] = Body{};
      rebased_ = false;
   }

   // A drag can carry a body far outside the window (the pointer keeps going), so where it's put
   // and how fast it's thrown are kept to what the tether can bring back.
   static constexpr double kMaxPlace = 4.0, kMaxSpeed = 6.0;
   void place(int i, double x, double y) {
      if (i < 0 || i >= count_ || !std::isfinite(x) || !std::isfinite(y))
         return;
      const double r = std::hypot(x, y), k = r > kMaxPlace ? kMaxPlace / r : 1.0;
      bodies_[i].x = x * k, bodies_[i].y = y * k;
      rebased_ = false;
   }
   void setVelocity(int i, double vx, double vy) {
      if (i < 0 || i >= count_ || !std::isfinite(vx) || !std::isfinite(vy))
         return;
      const double v = std::hypot(vx, vy), k = v > kMaxSpeed ? kMaxSpeed / v : 1.0;
      bodies_[i].vx = vx * k, bodies_[i].vy = vy * k;
      rebased_ = false;
   }
   void kick(int i, double dvx, double dvy) {
      if (i < count_)
         bodies_[i].vx += dvx, bodies_[i].vy += dvy;
   }
   void scaleVelocity(int i, double ratio) {
      if (i < count_)
         bodies_[i].vx *= ratio, bodies_[i].vy *= ratio;
      rebased_ = false;
   }

   // Advances `dt` time units. Close passes take smaller steps so slingshots stay accurate.
   void step(double dt, const double *mass, double gravity) {
      // A drag, throw, new speed or new mass sets the energy to keep; a kick doesn't, so it dies away.
      bool massChanged = false;
      for (int i = 0; i < count_; ++i)
         massChanged = massChanged || mass[i] != mass_[i];
      if (!rebased_ || massChanged)
         rebase(mass, gravity);
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
      loosen(dt, mass, gravity);
      recentre(mass);
      thermostat(dt, mass, gravity);
   }

   // The total energy: motion, the softened gravity, and the tether's spring.
   double energy(const double *mass, double gravity) const {
      double e = 0.0;
      for (int i = 0; i < count_; ++i) {
         const Body &b = bodies_[i];
         e += 0.5 * mass[i] * (b.vx * b.vx + b.vy * b.vy);
         const double out = std::max(0.0, std::hypot(b.x, b.y) - kTether);
         e += mass[i] * 0.5 * kSpring * out * out;
         for (int j = i + 1; j < count_; ++j) {
            const double dx = bodies_[j].x - b.x, dy = bodies_[j].y - b.y;
            const double r2 = dx * dx + dy * dy + kSoft * kSoft, r = std::sqrt(r2);
            e -= gravity * mass[i] * mass[j] / r;
         }
      }
      return e;
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
   // Softening spreads each body's pull over about kSoft (about the size a body is drawn), so close
   // passes stay finite and smooth. A pair bound tighter than an orbit of kPairSize across is eased
   // loose at kLoosen per time unit. The tether pulls back bodies flung past kTether. The energy settles
   // back to its target over about kSettle time units (a few seconds).
   //
   // These used to be a softening of 0.05 and a core inside 0.16 where gravity turned repulsive. That
   // kept pairs apart, but as a wall: a body falling in too straight hit it and reflected back the way
   // it came, about four close passes in ten turning the wrong way, which read as a bounce.
   static constexpr double kSoft = 0.15, kPairSize = 0.4, kLoosen = 2.0;
   static constexpr double kTether = 2.4, kSpring = 3.0, kSettle = 4.0;

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
            const double pull = kSpring * (r - kTether) / r;
            ax -= bodies_[i].x * pull;
            ay -= bodies_[i].y * pull;
            // And a little drag out there, so an escape is caught and turned around, not bounced, but
            // only while there's energy to spare: drag that always ran would drain the system, and the
            // bodies would fall into ever tighter pairs.
            if (excess_ > 0.0) {
               ax -= 0.4 * bodies_[i].vx * (r - kTether);
               ay -= 0.4 * bodies_[i].vy * (r - kTether);
            }
         }
         ax_[i] = ax;
         ay_[i] = ay;
      }
   }

   // Takes the energy now as the one to keep, per unit of gravity (a stronger gravity gives the
   // same shapes, only faster, and so proportionally more energy).
   void rebase(const double *mass, double gravity) {
      for (int i = 0; i < kMax; ++i)
         mass_[i] = i < count_ ? mass[i] : 0.0;
      target_ = energy(mass, gravity) / std::max(0.01, gravity);
      excess_ = 0.0;
      rebased_ = true;
   }

   // Eases the energy back to its target by scaling every speed alike, so no path bends; the drag
   // past the tether also catches what a kick put in.
   void thermostat(double dt, const double *mass, double gravity) {
      const double target = target_ * std::max(0.01, gravity);
      const double e = energy(mass, gravity);
      excess_ = e - target;
      double kinetic = 0.0;
      for (int i = 0; i < count_; ++i)
         kinetic += 0.5 * mass[i] * (bodies_[i].vx * bodies_[i].vx + bodies_[i].vy * bodies_[i].vy);
      if (kinetic < 1e-6)
         return;
      const double k = std::sqrt(std::clamp(1.0 - excess_ * std::min(1.0, dt / kSettle) / kinetic, 0.9, 1.1));
      for (int i = 0; i < count_; ++i)
         bodies_[i].vx *= k, bodies_[i].vy *= k;
   }

   // Two bodies bound tighter than an orbit of kPairSize would circle each other closely for good (the
   // others keep taking energy from them, and nothing hands it back). So such a pair's speed relative
   // to each other is raised a little each step, in proportion to how much too deep it is, which
   // widens its orbit smoothly over a second or so. Then every speed is scaled down alike by what
   // that added, so the total stays where the thermostat keeps it and the energy comes, in effect,
   // from everything but the pair's own circling. Two bodies alone are left as they are: the pair is
   // the whole system then, and its orbit is what it was given.
   void loosen(double dt, const double *mass, double gravity) {
      if (count_ < 3)
         return;
      const double rate = kLoosen * std::sqrt(std::max(0.01, gravity)) * dt;
      double added = 0.0;
      for (int i = 0; i < count_; ++i)
         for (int j = i + 1; j < count_; ++j) {
            Body &a = bodies_[i], &b = bodies_[j];
            const double dx = b.x - a.x, dy = b.y - a.y, dvx = b.vx - a.vx, dvy = b.vy - a.vy;
            const double r = std::sqrt(dx * dx + dy * dy + kSoft * kSoft), m = mass[i] + mass[j];
            const double mu = mass[i] * mass[j] / m, gm = gravity * mass[i] * mass[j];
            const double e = 0.5 * mu * (dvx * dvx + dvy * dvy) - gm / r, floor = -gm / (2.0 * kPairSize);
            if (e >= floor)
               continue;
            const double k = rate * std::min(1.0, (floor - e) / -floor);
            a.vx -= k * dvx * mass[j] / m, a.vy -= k * dvy * mass[j] / m;
            b.vx += k * dvx * mass[i] / m, b.vy += k * dvy * mass[i] / m;
            added += 0.5 * mu * ((1.0 + k) * (1.0 + k) - 1.0) * (dvx * dvx + dvy * dvy);
         }
      if (added <= 0.0)
         return;
      double kinetic = 0.0;
      for (int i = 0; i < count_; ++i)
         kinetic += 0.5 * mass[i] * (bodies_[i].vx * bodies_[i].vx + bodies_[i].vy * bodies_[i].vy);
      const double scale = std::sqrt(std::max(0.5, 1.0 - added / std::max(1e-9, kinetic)));
      for (int i = 0; i < count_; ++i)
         bodies_[i].vx *= scale, bodies_[i].vy *= scale;
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
   double mass_[kMax] = {}, target_ = 0.0, excess_ = 0.0;
   bool rebased_ = false;
   int count_ = 3;
};

} // namespace jverb
