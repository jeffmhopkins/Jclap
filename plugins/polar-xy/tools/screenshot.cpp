// Renders the panel to a PNG without a host: polar-xy-screenshot out.png [width height]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "motion.h"
#include "params.h"
#include "polar.h"

namespace {

// A hand sweeps the controller most of the way round the pad and stops at X 104, Y 88; the output
// follows with the motion below, still swinging into place when the picture is taken.
constexpr double kSweep = 1.5;   // seconds
constexpr double kSeconds = 1.3; // shown this long after the start
constexpr double kOffset = 45.0; // the angle turned, so the two views differ

class DemoHost : public polar_xy::EditorHost {
public:
   DemoHost() {
      motion_.smoothing = 0.25;
      motion_.inertia = 0.3;
      hand(0.0, x_, y_);
      follower_.place(x_, y_);
   }

   // Moves the hand and the output `dt` seconds on.
   void advance(double dt) {
      time_ += dt;
      hand(time_, x_, y_);
      follower_.follow(x_, y_);
      follower_.step(dt, motion_);
   }

   double paramValue(clap_id id) const override {
      // Defaults, with the angle offset and some motion.
      if (id == polar_xy::kParamAngleOffset)
         return kOffset;
      if (id == polar_xy::kParamSmoothing)
         return motion_.smoothing * 1000.0;
      if (id == polar_xy::kParamInertia)
         return motion_.inertia * 100.0;
      return id < polar_xy::kParamCount ? polar_xy::kParams[id].def : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   std::string presetName() const override { return "Init"; }
   void setPresetName(const std::string &) override {}
   void dragInput(double, double) override {}
   void releaseInput() override {}
   void sendOutput(int) override {}
   void lastInput(int &x, int &y, int &channel) const override {
      x = toCC(x_);
      y = toCC(y_);
      channel = 0;
   }
   // What the plugin sends for where the output is now.
   bool lastOutput(int &angle, int &radius) const override {
      jclap::PolarSettings settings;
      settings.angleOffsetDeg = kOffset;
      double x, y;
      follower_.position(x, y);
      const jclap::PolarCC out = jclap::toPolar(x, y, settings);
      angle = out.angle;
      radius = out.radius;
      return true;
   }
   bool liveOutput(double &x, double &y, double &targetX, double &targetY) const override {
      follower_.position(x, y);
      follower_.target(targetX, targetY);
      return true;
   }

private:
   // The hand at `t` seconds, on the -1..+1 pad: round most of a turn, easing out and widening, to
   // rest at X 104, Y 88.
   static void hand(double t, double &x, double &y) {
      const double endX = jclap::ccToBipolar(104), endY = jclap::ccToBipolar(88);
      const double u = std::min(t / kSweep, 1.0), ease = 1.0 - (1.0 - u) * (1.0 - u);
      const double end = std::atan2(endY, endX), r = std::hypot(endX, endY);
      const double a = end + 1.55 * jclap::kPi * (1.0 - ease);
      const double reach = r * (0.45 + 0.55 * ease);
      x = reach * std::cos(a);
      y = reach * std::sin(a);
   }

   static int toCC(double v) { return static_cast<int>(std::lround(v >= 0.0 ? 64.0 + v * 63.0 : 64.0 + v * 64.0)); }

   jclap::MotionSettings motion_;
   jclap::Follower follower_;
   double time_ = 0.0, x_ = 0.0, y_ = 0.0;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 720;
   const int height = argc > 3 ? std::atoi(argv[3]) : 690;
   DemoHost host;
   return polar_xy::renderScreenshot(host, width, height, argv[1], kSeconds,
                                     [&host](double dt) { host.advance(dt); })
             ? 0
             : 1;
}
