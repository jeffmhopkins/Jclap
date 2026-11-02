// Touch Filter on its own: a resistive pad's skittery presses and releases, made up reading by reading.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "touch.h"

using namespace jclap;

static int failures = 0;

#define CHECK(cond)                                                \
   do {                                                            \
      if (!(cond)) {                                               \
         std::printf("%s:%d: %s\n", __FILE__, __LINE__, #cond);    \
         ++failures;                                               \
      }                                                            \
   } while (0)

namespace {

struct Out {
   double x, y, t;
};

struct Reading {
   double x, y, t;
};

// Feeds readings in, polling every half millisecond, and collects what comes out (until `until`).
std::vector<Out> run(const std::vector<Reading> &in, double window, double until) {
   TouchFilter f;
   std::vector<Out> out;
   size_t next = 0;
   for (double now = 0.0; now <= until; now += 0.0005) {
      while (next < in.size() && in[next].t <= now) {
         f.push(in[next].x, in[next].y, in[next].t, window);
         ++next;
      }
      double x, y;
      while (f.pop(now, window, x, y))
         out.push_back({x, y, now});
   }
   return out;
}

// A touch at (0.5, 0.3) that drags right to (0.6, 0.3) over 150 ms, a reading every 5 ms, with a
// skittery press before it and a release that slides off and skitters away after, 2 ms apart.
std::vector<Reading> skitteryTouch(double t0) {
   std::vector<Reading> r;
   double t = t0;
   for (auto [x, y] : {std::pair{-0.9, -0.9}, {0.1, -0.5}, {0.42, 0.22}}) {
      r.push_back({x, y, t});
      t += 0.002;
   }
   for (int i = 0; i <= 30; ++i, t += 0.005)
      r.push_back({0.5 + 0.1 * i / 30.0, 0.3 + (i % 2 ? 0.01 : -0.01), t});
   for (auto [x, y] : {std::pair{0.55, 0.1}, {0.2, -0.4}, {-0.8, -0.9}}) {
      r.push_back({x, y, t});
      t += 0.002;
   }
   return r;
}

bool onTouch(const Out &o) { return o.x >= 0.45 && o.x <= 0.61 && o.y >= 0.28 && o.y <= 0.32; }

} // namespace

int main() {
   // The press and the release are thrown away: everything that comes out is where the finger was,
   // starting a window after the press, and ending a window before the release, which is the last
   // part of the drag thrown away with it.
   for (double window : {0.01, 0.02, 0.04}) {
      const auto in = skitteryTouch(0.0);
      const auto out = run(in, window, 0.5);
      CHECK(out.size() > 30 - window / 0.005 * 2);
      for (const auto &o : out)
         CHECK(onTouch(o));
      if (!out.empty()) {
         CHECK(out.front().t >= window - 1e-9 && out.front().t < window + 0.006);
         CHECK(out.back().x <= 0.6 && out.back().x > 0.6 - 0.1 * (window + 0.01) / 0.15);
      }
   }

   // Without the filter's edges trimmed (the readings as they come), the same touch would visit the
   // skitter: the test's touch really is skittery.
   {
      int off = 0;
      for (const auto &r : skitteryTouch(0.0))
         off += !onTouch({r.x, r.y, r.t});
      CHECK(off >= 6);
   }

   // A second touch somewhere else after lifting: the same again, with nothing of the first's release.
   {
      auto in = skitteryTouch(0.0);
      for (auto r : skitteryTouch(0.3)) {
         r.x = -r.x; // mirrored, so it lands on the other side
         in.push_back(r);
      }
      const auto out = run(in, 0.02, 0.8);
      int first = 0, second = 0, stray = 0;
      for (const auto &o : out) {
         if (onTouch(o))
            ++first;
         else if (onTouch({-o.x, o.y, o.t}))
            ++second;
         else
            ++stray;
      }
      CHECK(first > 20 && second > 20 && stray == 0);
   }

   // X and Y a millisecond apart are one reading: the new X never goes out with the old Y.
   {
      const std::vector<Reading> in = {{0.0, 0.0, 0.0},   {0.0, 0.0, 0.002}, {0.0, 0.0, 0.004},
                                       {0.0, 0.0, 0.03},  {0.2, 0.0, 0.05},  {0.2, 0.2, 0.051},
                                       {0.2, 0.2, 0.056}};
      const auto out = run(in, 0.01, 0.2);
      bool half = false;
      for (const auto &o : out)
         half = half || (o.x == 0.2 && o.y == 0.0);
      CHECK(!half);
      CHECK(!out.empty() && out.back().x == 0.2 && out.back().y == 0.2);
   }

   // A slow drag on a controller that only sends when the value changes, its readings far apart:
   // every one comes out, each a window late.
   {
      std::vector<Reading> in;
      for (int i = 0; i < 10; ++i)
         in.push_back({0.3 + 0.02 * i, 0.0, 0.1 * i});
      in.insert(in.begin() + 1, {0.3, 0.0, 0.01}); // the press settles
      const auto out = run(in, 0.02, 1.2);
      CHECK(out.size() == 10);
      if (out.size() == 10)
         CHECK(std::abs(out.back().x - 0.48) < 1e-9 && std::abs(out.back().t - 0.92) < 0.001);
   }

   // A tap: two readings that agree and then nothing. It comes out at the end of its press.
   {
      const auto out = run({{0.5, 0.5, 0.0}, {0.51, 0.5, 0.005}}, 0.02, 0.2);
      CHECK(out.size() == 1 && std::abs(out[0].x - 0.51) < 1e-9 && std::abs(out[0].t - 0.02) < 0.001);
   }

   // A lone blip (a release that sends the pad's rest position once) never comes out.
   {
      const auto out = run({{0.5, 0.5, 0.0}, {0.5, 0.5, 0.005}, {0.5, 0.5, 0.03}, {-1.0, -1.0, 0.04}}, 0.02, 0.3);
      for (const auto &o : out)
         CHECK(o.x == 0.5);
   }

   if (failures)
      std::printf("%d failure(s)\n", failures);
   else
      std::printf("all touch filter tests passed\n");
   return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
