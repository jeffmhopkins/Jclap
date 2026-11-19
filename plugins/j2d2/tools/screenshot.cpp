// Renders the panel to a PNG without a host, showing a preset:
//
//    j2d2-screenshot out.png [page [width height [preset]]]
//
// page: 0 VOICE, 1 DRONES (AMP), 2 DRONES (ENVELOPES), 3 DRONES (SHAPE LFOS). The preset defaults to
// Astromech on the VOICE page and Hangar Bay on the others.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "editor.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace j2d2;

class DemoHost : public EditorHost {
public:
   explicit DemoHost(const std::string &preset) : name_(preset) {
      for (int i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &p : factoryPresets())
         if (p.name == preset)
            for (const auto &[id, v] : p.values)
               values_[id] = v;
   }
   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   void outputPeaks(float &l, float &r) override { l = r = 0.0f; }
   void readScope(float *out, size_t count) override {
      for (size_t i = 0; i < count; ++i)
         out[i] = 0.0f;
   }
   Voice voice(int) const override { return {}; }
   float lfo(int) const override { return 0.0f; }
   std::string presetName() const override { return name_; }
   void setPresetName(const std::string &name) override { name_ = name; }

private:
   double values_[kParamCount];
   std::string name_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [page [width height [preset]]]\n", argv[0]);
      return 1;
   }
   const int page = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : 1100;
   const int height = argc > 4 ? std::atoi(argv[4]) : 680;
   const std::string preset = argc > 5 ? argv[5] : page == 0 ? "Astromech" : "Hangar Bay";
   DemoHost host(preset);
   return renderScreenshot(host, page, width, height, argv[1]) ? 0 : 1;
}
