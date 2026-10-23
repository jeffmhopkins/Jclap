// Renders the panel to a PNG without a host: plugin_template-screenshot out.png [page [width height]]

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "params.h"

namespace {

using namespace plugin_template;

class DemoHost : public EditorHost {
public:
   double paramValue(clap_id id) const override {
      switch (id) {
      case kGain:
         return -4.5;
      case kPan:
         return 0.3;
      case kWidth:
         return 1.4;
      case kWave:
         return 2.0;
      case kTranspose:
         return 12.0;
      default:
         return id < kParamCount ? kParams[id].defaultValue : 0.0;
      }
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   float outputPeak() override { return 0.42f; }
   void readScope(float *out, size_t count) override {
      // A decaying saw, about four cycles.
      for (size_t i = 0; i < count; ++i) {
         const double t = static_cast<double>(i) / count;
         const double phase = t * 4.0 - std::floor(t * 4.0);
         out[i] = static_cast<float>((2.0 * phase - 1.0) * 0.7 * (1.0 - 0.5 * t));
      }
   }
   void noteOn(int, float) override {}
   void noteExpression(int, float, float, float) override {}
   void noteOff(int) override {}
   bool voice(Voice &out) const override {
      out = {50, 1.6f, 0.7f, 0.75f};
      return true;
   }
   bool noteHeld(int note) const override { return note == 60 || note == 64 || note == 67; }
   uint32_t noteCount() const override { return ++count_; }

private:
   mutable uint32_t count_ = 0;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [page [width height]]\n", argv[0]);
      return 1;
   }
   const int page = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : 600;
   const int height = argc > 4 ? std::atoi(argv[4]) : 360;
   DemoHost host;
   return renderScreenshot(host, page, width, height, argv[1]) ? 0 : 1;
}
