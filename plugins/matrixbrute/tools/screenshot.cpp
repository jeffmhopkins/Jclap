// Renders the panel to a PNG without a host: matrixbrute-screenshot out.png [width height]

#include <cstdio>
#include <cstdlib>

#include "cc_map.h"
#include "editor.h"
#include "patches.h"

namespace {

// The synth's A01 Juggernaut, as the plugin loads it, with the 14-bit and follow settings on. The
// knobs a preset doesn't store (the sequencer's rate, swing and gate, the mod wheel) haven't been
// heard yet, and some knobs show host modulation, so every style of control shows up.
class DemoHost : public mb::EditorHost {
public:
   static const mb::HardwarePatch &patch() { return *mb::findFactoryPatch("A01 Juggernaut"); }
   double paramValue(clap_id id) const override {
      if (id == mb::kChannelParamId)
         return 1;
      if (id == mb::kFineParamId || id == mb::kFollowParamId)
         return 1;
      const int i = mb::indexOfCc(static_cast<int>(id));
      if (i < 0)
         return 0;
      for (auto [cc, v] : patch().values)
         if (cc == static_cast<int>(id))
            return v;
      return mb::defaultValue(mb::kCcParams[i]);
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
   bool known(int cc) const override { return !(cc >= 116 && cc <= 118) && cc != 1; }
   uint32_t heardCount() const override { return 0; }
   int synthPreset() const override { return patch().preset; }
   void loadProgram(const mb::HardwarePatch &) override {}
   std::string presetName() const override { return mb::displayName(patch()); }
   void setPresetName(const std::string &) override {}
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1760;
   const int height = argc > 3 ? std::atoi(argv[3]) : 1240;
   DemoHost host;
   return mb::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
