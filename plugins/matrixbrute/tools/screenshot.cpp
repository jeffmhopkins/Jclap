// Renders the panel to a PNG without a host: matrixbrute-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "cc_map.h"
#include "editor.h"

namespace {

// A spread of values, with most knobs heard from the synth and a few not yet, and some host
// modulation, so every style of control shows up.
class DemoHost : public mb::EditorHost {
public:
   double paramValue(clap_id id) const override {
      if (id == mb::kChannelParamId)
         return 1;
      if (id == mb::kFineParamId || id == mb::kFollowParamId)
         return 1;
      const int i = mb::indexOfCc(static_cast<int>(id));
      if (i < 0)
         return 0;
      switch (id) {
      case 9: return 40;   // VCO1 sub
      case 10: return 110; // VCO1 saw
      case 12: return 0;
      case 13: return 30;
      case 15: return 20;
      case 16: return 100;
      case 17: return 0;
      case 23: return 78;  // Steiner cutoff
      case 25: return 52;  // ladder cutoff
      case 28: return 90;
      case 29: return 110;
      case 30: return 40;
      default: return mb::kCcParams[i].bipolar ? 64 + (i * 13) % 40 - 20 : (i * 37) % 128;
      }
   }
   // Every seventh knob shows some host modulation.
   double paramModulated(clap_id id) const override {
      const int i = mb::indexOfCc(static_cast<int>(id));
      return i < 0 ? paramValue(id) : mb::modulated(paramValue(id), i % 7 == 3 ? 20.0 : 0.0);
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   int channel() const override { return 1; }
   void setChannel(int) override {}
   void sendAll() override {}
   // The sequencer, macros and LFO 2 haven't been heard yet.
   bool known(int cc) const override { return !(cc >= 116 && cc <= 118) && cc != 2 && cc != 3 && cc != 92 && cc != 93; }
   uint32_t heardCount() const override { return 0; }
   int heardProgram() const override { return 12; }
   std::string presetName() const override { return "Init"; }
   void setPresetName(const std::string &) override {}
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1800;
   const int height = argc > 3 ? std::atoi(argv[3]) : 1100;
   DemoHost host;
   return mb::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
