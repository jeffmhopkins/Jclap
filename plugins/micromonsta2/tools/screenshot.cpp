// Renders the panel to a PNG without a host: micromonsta2-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "cc_map.h"
#include "editor.h"

namespace {

// Fills the knobs with a spread of values so every arc style shows up.
class DemoHost : public mm2::EditorHost {
public:
   double ccValue(int index) const override { return (index * 37) % 128; }
   void beginEdit(int) override {}
   void setValue(int, double) override {}
   void endEdit(int) override {}
   int channel() const override { return 1; }
   void setChannel(int) override {}
   void sendAll() override {}
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1160;
   const int height = argc > 3 ? std::atoi(argv[3]) : 732;
   DemoHost host;
   return mm2::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
