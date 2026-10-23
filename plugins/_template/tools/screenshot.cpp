// Renders the panel to a PNG without a host: plugin_template-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "params.h"

namespace {

class DemoHost : public plugin_template::EditorHost {
public:
   double paramValue(clap_id id) const override {
      const double values[] = {-4.5, 0.3, 0.0, 1.4, 0.0};
      return id < plugin_template::kParamCount ? values[id] : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   float outputPeak() override { return 0.42f; }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 600;
   const int height = argc > 3 ? std::atoi(argv[3]) : 320;
   DemoHost host;
   return plugin_template::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
