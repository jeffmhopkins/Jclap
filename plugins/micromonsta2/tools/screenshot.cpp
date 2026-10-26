// Renders the panel to a PNG without a host: micromonsta2-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "cc_map.h"
#include "editor.h"

namespace {

// Fills the knobs with a spread of values so every arc style shows up.
class DemoHost : public mm2::EditorHost {
public:
   double paramValue(clap_id id) const override { return (mm2::indexOfCc(static_cast<int>(id)) * 37) % 128; }
   // Every fifth knob shows some host modulation.
   double paramModulated(clap_id id) const override {
      return mm2::modulated(paramValue(id), mm2::indexOfCc(static_cast<int>(id)) % 5 == 0 ? 25.0 : 0.0);
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   int channel() const override { return 1; }
   void setChannel(int) override {}
   void sendAll() override {}
   void loadProgram(const mm2::HardwarePatch &) override {}
   // A program with a few mod matrix slots routed, so their labels show.
   int program() const override { return 12; }
   mm2::ModRoute route(int slot) const override {
      static const mm2::ModRoute routes[mm2::kModSlots] = {{18, 12}, {21, 1}, {13, 13}, {5, 8}};
      return routes[slot];
   }
   std::string presetName() const override { return "P012 OctaBass"; }
   void setPresetName(const std::string &) override {}
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
