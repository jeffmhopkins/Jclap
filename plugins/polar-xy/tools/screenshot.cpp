// Renders the panel to a PNG without a host: polar-xy-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "params.h"

namespace {

class DemoHost : public polar_xy::EditorHost {
public:
   double paramValue(clap_id id) const override {
      // Defaults, with the angle turned 45 degrees so the two views differ.
      if (id == polar_xy::kParamAngleOffset)
         return 45.0;
      return id < polar_xy::kParamCount ? polar_xy::kParams[id].def : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   void lastInput(int &x, int &y) const override {
      x = 104;
      y = 88;
   }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 720;
   const int height = argc > 3 ? std::atoi(argv[3]) : 480;
   DemoHost host;
   return polar_xy::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
