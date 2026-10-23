// Renders the panel to a PNG without a host: basic_synth-screenshot out.png [page [width height]]
// Page 0 shows the keyboard, page 1 the MPE note grid.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "params.h"

namespace {

using namespace basic_synth;

class DemoHost : public EditorHost {
public:
   double paramValue(clap_id id) const override {
      switch (id) {
      case kWave:
         return 4.0;
      case kPulseWidth:
         return 0.3;
      case kCutoff:
         return 0.55;
      case kResonance:
         return 0.45;
      case kLfoToCutoff:
         return 1.5;
      case kLfoToVca:
         return 0.3;
      default:
         return id < kParamCount ? kParams[id].defaultValue : 0.0;
      }
   }
   // Some host modulation, to show how the knobs and envelopes draw it.
   double paramModulated(clap_id id) const override {
      switch (id) {
      case kCutoff:
         return 0.68;
      case kAmpDecay:
         return 700.0;
      case kAmpSustain:
         return 0.45;
      case kFilterAttack:
         return 300.0;
      default:
         return paramValue(id);
      }
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   void readScope(float *out, size_t count) override {
      // A filtered pulse, about four cycles: a rounded square with a little ring.
      for (size_t i = 0; i < count; ++i) {
         const double t = static_cast<double>(i) / count * 4.0, phase = t - std::floor(t);
         const double edge = phase < 0.3 ? phase : phase - 0.3;
         const double level = phase < 0.3 ? 0.7 : -0.3;
         out[i] = static_cast<float>(level * (1.0 - std::exp(-edge * 40.0)) +
                                     0.15 * std::exp(-edge * 25.0) * std::sin(edge * 120.0) * (phase < 0.3 ? 1 : -1));
      }
   }
   void noteOn(int, float) override {}
   void noteExpression(int, float, float, float) override {}
   void noteOff(int) override {}
   bool lfo(float &value) const override {
      value = 0.6f;
      return true;
   }
   void voices(std::vector<Voice> &out) const override {
      // A chord, each note bent and pressed a little differently, as MPE playing looks.
      out = {{48, -0.4f, 0.3f, 0.5f}, {55, 0.0f, 0.6f, 0.7f}, {60, 0.9f, 0.85f, 0.3f}, {64, 0.2f, 0.5f, 0.55f}};
   }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [page [width height]]\n", argv[0]);
      return 1;
   }
   const int page = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : 720;
   const int height = argc > 4 ? std::atoi(argv[4]) : 820;
   DemoHost host;
   return renderScreenshot(host, page, width, height, argv[1]) ? 0 : 1;
}
