// Renders the panel to a PNG without a host: polar-xy-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "params.h"
#include "polar.h"

namespace {

class DemoHost : public polar_xy::EditorHost {
public:
   double paramValue(clap_id id) const override {
      // Defaults, with the angle turned 45 degrees so the two views differ, and some motion.
      if (id == polar_xy::kParamAngleOffset)
         return 45.0;
      if (id == polar_xy::kParamSmoothing)
         return 250.0;
      if (id == polar_xy::kParamInertia)
         return 30.0;
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
      x = 104;
      y = 88;
      channel = 0;
   }
   // What the plugin sends for that input with the 45 degree offset.
   bool lastOutput(int &angle, int &radius) const override {
      jclap::PolarSettings settings;
      settings.angleOffsetDeg = 45.0;
      const jclap::PolarCC out = jclap::toPolar(104, 88, settings);
      angle = out.angle;
      radius = out.radius;
      return true;
   }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 720;
   const int height = argc > 3 ? std::atoi(argv[3]) : 612;
   DemoHost host;
   return polar_xy::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
