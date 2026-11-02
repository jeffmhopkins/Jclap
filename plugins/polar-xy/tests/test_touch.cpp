// Touch Filter on its own: a resistive pad's skittery presses and releases, made up reading by reading.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
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

   // With a touch CC: a lift-off that slides gently away (no jump to give it away) is still thrown
   // away, readings while lifted are ignored, a press needs only one reading, and the touch comes
   // out as Down before the first reading and Up after the last.
   {
      TouchFilter f;
      const double w = 0.02;
      std::vector<std::pair<TouchFilter::Event, double>> events; // event, x
      std::vector<double> times;
      const auto poll = [&](double from, double to) {
         for (double now = from; now <= to; now += 0.0005) {
            double x, y;
            TouchFilter::Event e;
            while (f.pop(now, w, x, y, e)) {
               events.push_back({e, x});
               times.push_back(now);
            }
         }
      };
      f.push(-0.9, -0.9, 0.0, w); // noise before any touch... the filter doesn't know yet
      f.touch(false, 0.001, w);    // ...until the touch CC says the finger is up
      poll(0.0, 0.05);
      f.push(0.8, 0.8, 0.06, w); // lifted: ignored
      poll(0.05, 0.1);
      f.touch(true, 0.1, w);
      f.push(0.5, 0.3, 0.102, w); // one reading, then the finger holds still: out a window later
      poll(0.1, 0.2);
      for (int i = 0; i < 10; ++i) { // a slow drag...
         f.push(0.5 + 0.01 * i, 0.3, 0.2 + 0.005 * i, w);
         poll(0.2 + 0.005 * i, 0.2 + 0.005 * (i + 1) - 0.0005);
      }
      for (int i = 1; i <= 3; ++i) { // ...then the lift-off slides away in small steps
         f.push(0.59 + 0.1 * i, 0.3 + 0.1 * i, 0.25 + 0.003 * i, w);
         poll(0.25 + 0.003 * i, 0.25 + 0.003 * (i + 1) - 0.0005);
      }
      f.touch(false, 0.262, w);
      poll(0.262, 0.4);
      CHECK(!events.empty() && events.front().first == TouchFilter::Event::Down);
      CHECK(events.size() >= 2 && events[1].first == TouchFilter::Event::Move && events[1].second == 0.5 &&
            std::abs(times[1] - 0.122) < 0.001);
      CHECK(!events.empty() && events.back().first == TouchFilter::Event::Up);
      int downs = 0, ups = 0;
      for (const auto &[e, x] : events) {
         downs += e == TouchFilter::Event::Down;
         ups += e == TouchFilter::Event::Up;
         if (e == TouchFilter::Event::Move)
            CHECK(x >= 0.5 && x <= 0.59 + 1e-9);
      }
      CHECK(downs == 1 && ups == 1);
   }

   // A touch too short for anything to go out: no Down, so no Up either.
   {
      TouchFilter f;
      int n = 0;
      double x, y;
      TouchFilter::Event e;
      f.touch(true, 0.0, 0.02);
      f.push(0.1, 0.1, 0.001, 0.02);
      for (double now = 0.0; now < 0.015; now += 0.0005)
         while (f.pop(now, 0.02, x, y, e))
            ++n;
      f.touch(false, 0.015, 0.02);
      for (double now = 0.015; now < 0.1; now += 0.0005)
         while (f.pop(now, 0.02, x, y, e))
            ++n;
      CHECK(n == 0);
   }

   // With the touch CC, a jump within the touch still needs two readings that agree: a lone lift-off
   // reading more than a window before the touch CC goes off (as Jeff's pad sends them, about 15 ms
   // early, with a 10 ms window) never goes out.
   {
      TouchFilter f;
      const double w = 0.01;
      std::vector<double> xs;
      const auto poll = [&](double from, double to) {
         double x, y;
         TouchFilter::Event e;
         for (double now = from; now <= to; now += 0.0005)
            while (f.pop(now, w, x, y, e))
               if (e == TouchFilter::Event::Move)
                  xs.push_back(x);
      };
      f.touch(true, 0.0, w);
      for (int i = 0; i < 10; ++i) {
         f.push(0.2, 0.2, 0.002 + 0.01 * i, w);
         poll(0.002 + 0.01 * i, 0.002 + 0.01 * (i + 1) - 0.0005);
      }
      f.push(0.9, 0.9, 0.102, w); // the lift-off, alone
      poll(0.102, 0.117);
      f.touch(false, 0.1175, w);
      poll(0.1175, 0.2);
      CHECK(!xs.empty());
      for (double x : xs)
         CHECK(x == 0.2);
   }

   if (failures)
      std::printf("%d failure(s)\n", failures);
   else
      std::printf("all touch filter tests passed\n");
   return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
