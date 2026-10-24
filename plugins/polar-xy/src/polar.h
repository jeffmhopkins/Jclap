// Pure XY -> polar math for the Omnisphere Orb. No CLAP dependencies so it can be unit tested.
#pragma once

#include <algorithm>
#include <cmath>

namespace jclap {

constexpr double kPi = 3.14159265358979323846;

// Maps a 0..127 CC value to -1..+1 with 64 as dead center, so both halves reach full scale.
inline double ccToBipolar(int v) {
   v = std::clamp(v, 0, 127);
   return v >= 64 ? (v - 64) / 63.0 : (v - 64) / 64.0;
}

// Around the centre a controller's ±1 jitter would swing the angle across quadrants, so inside this
// radius (on the -1..+1 scale; one CC step is about 0.016) the angle holds and the radius is 0. The
// radius is measured from its edge, so it still starts at 1.
constexpr double kDeadZone = 0.03;

enum class RadiusMode { ClampToCircle = 0, FillSquare = 1 };

struct PolarSettings {
   double angleOffsetDeg = 0.0; // rotates where Orb angle 0 lands
   bool clockwise = false;      // false: angle grows counter-clockwise (math convention)
   RadiusMode radiusMode = RadiusMode::ClampToCircle;
};

struct PolarCC {
   int angle = 0;       // 0..126 in steps of 360/127 degrees (127 would be 360, the same as 0)
   int radius = 0;      // 0..127, 0 = center, 127 = edge
   bool hasAngle = false; // false at dead center, where the angle is undefined
};

inline double wrapDegrees(double deg) {
   deg = std::fmod(deg, 360.0);
   if (deg < 0.0)
      deg += 360.0;
   return deg;
}

// xcc/ycc are raw 0..127 CC values; Y grows upward.
inline PolarCC toPolar(int xcc, int ycc, const PolarSettings &s) {
   const double x = ccToBipolar(xcc);
   const double y = ccToBipolar(ycc);

   PolarCC out;
   double r = s.radiusMode == RadiusMode::FillSquare ? std::max(std::abs(x), std::abs(y))
                                                     : std::hypot(x, y);
   r = std::clamp(r, 0.0, 1.0);
   if (r < kDeadZone)
      return out;
   out.radius = static_cast<int>(std::lround((r - kDeadZone) / (1.0 - kDeadZone) * 127.0));

   double deg = std::atan2(y, x) * 180.0 / kPi;
   if (s.clockwise)
      deg = -deg;
   deg = wrapDegrees(deg + s.angleOffsetDeg);
   // A 0..127 CC read as v / 127 of a turn makes 127 the same angle as 0, so a turn is 0..126 and
   // what would round to 127 is sent as 0.
   out.angle = static_cast<int>(std::lround(deg / 360.0 * 127.0)) % 127;
   out.hasAngle = true;
   return out;
}

} // namespace jclap
