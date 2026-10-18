#include <cstdio>
#include <cstdlib>

#include "polar.h"

using namespace jclap;

static int failures = 0;

#define CHECK_EQ(a, b)                                                                             \
   do {                                                                                            \
      const auto va = (a);                                                                         \
      const auto vb = (b);                                                                         \
      if (va != vb) {                                                                              \
         std::printf("%s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, (int)va, (int)vb); \
         ++failures;                                                                               \
      }                                                                                            \
   } while (0)

int main() {
   const PolarSettings def;

   // Center: no radius, no angle.
   auto c = toPolar(64, 64, def);
   CHECK_EQ(c.radius, 0);
   CHECK_EQ(c.hasAngle, false);

   // Cardinal edges: full radius, quarter turns counter-clockwise from +X.
   auto right = toPolar(127, 64, def);
   CHECK_EQ(right.radius, 127);
   CHECK_EQ(right.angle, 0);
   CHECK_EQ(toPolar(64, 127, def).angle, 32);  // 90 deg  -> 31.75
   CHECK_EQ(toPolar(0, 64, def).angle, 64);    // 180 deg -> 63.5
   CHECK_EQ(toPolar(64, 0, def).angle, 95);    // 270 deg -> 95.25
   CHECK_EQ(toPolar(0, 64, def).radius, 127);

   // Corners clamp to the unit circle by default, and reach full radius in fill-square mode.
   CHECK_EQ(toPolar(127, 127, def).radius, 127);
   CHECK_EQ(toPolar(96, 96, def).radius, 91);  // ~0.508 * sqrt(2) * 127
   PolarSettings fill;
   fill.radiusMode = RadiusMode::FillSquare;
   CHECK_EQ(toPolar(96, 96, fill).radius, 65);
   CHECK_EQ(toPolar(127, 127, fill).angle, 16); // 45 deg

   // Just below +X wraps to the top of the CC range rather than going negative.
   auto below = toPolar(127, 63, def);
   CHECK_EQ(below.angle, 127);

   // Clockwise flips the sense; offset rotates and wraps.
   PolarSettings cw;
   cw.clockwise = true;
   CHECK_EQ(toPolar(64, 127, cw).angle, 95);
   PolarSettings off;
   off.angleOffsetDeg = 90.0;
   CHECK_EQ(toPolar(127, 64, off).angle, 32);
   CHECK_EQ(toPolar(64, 0, off).angle, 0);    // 270 + 90 wraps to 0

   // Out-of-range input is clamped.
   CHECK_EQ(toPolar(200, 64, def).radius, 127);

   if (failures)
      std::printf("%d failure(s)\n", failures);
   else
      std::printf("all polar tests passed\n");
   return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
