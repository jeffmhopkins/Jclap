// Renders the panel to a PNG without a host:
//    jbreath-screenshot out.png [width height [frames [player-tab [routes [armed-source]]]]]
// A four-note reed chord is playing, with breath loops drawn from synthetic tones.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "editor.h"
#include "params.h"

namespace {

using namespace jbreath;

class DemoHost : public EditorHost {
public:
   DemoHost() {
      for (int i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      values_[kBreath] = 1.1;
      values_[kEmbouchure] = 0.45;
      values_[kVibDepth] = 0.03;
      values_[kVibPitch] = 6.0;
      values_[kKick] = 0.35;
      values_[kNoise] = 0.8;
      values_[kLfo1Rate] = 0.6;
      values_[kSwellTime] = 6.0;
      // A few routes, as a player would map them.
      const struct {
         int source;
         clap_id dest;
         double amount;
      } routes[] = {{kSrcPress, kBreath, 0.3}, {kSrcSlide, kEmbouchure, 0.6}, {kSrcStrike, kKick, 0.5},
                    {kSrcVapor, kNoise, 0.3},  {kSrcFold, kEdge, -0.25},     {kSrcLfo1, kFine, 0.1},
                    {kSrcSwell, kVibDepth, 0.4}};
      int r = 0;
      for (const auto &route : routes) {
         values_[routeParam(r, kRouteSource)] = route.source;
         values_[routeParam(r, kRouteDestination)] = destinationOf(route.dest);
         values_[routeParam(r, kRouteAmount)] = route.amount;
         ++r;
      }
   }
   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   double paramModulated(clap_id id) const override { return id == kEmbouchure ? 0.58 : paramValue(id); }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}
   std::string presetName() const override { return "Wooden Reed"; }
   void setPresetName(const std::string &) override {}

   // D3 and A3 held, F#4 swelling, E5 let go and fading.
   static constexpr int kNotes[4] = {50, 57, 66, 76};
   void voices(std::vector<Voice> &out) const override {
      out.assign(kVoiceCount, Voice{});
      const float breath[4] = {0.62f, 0.7f, 0.85f, 0.3f}, pressure[4] = {0.5f, 0.62f, 0.85f, 0.2f};
      const float timbre[4] = {0.5f, 0.42f, 0.66f, 0.5f}, level[4] = {0.08f, 0.1f, 0.14f, 0.03f};
      for (int i = 0; i < 4; ++i) {
         const float hz = 440.0f * std::pow(2.0f, (kNotes[i] - 69) / 12.0f);
         out[i] = {kNotes[i], hz, breath[i], pressure[i], timbre[i], level[i], i == 3};
         float *src = out[i].sources.data();
         src[kSrcStrike] = 0.7f;
         src[kSrcPress] = pressure[i];
         src[kSrcGlide] = 0.1f;
         src[kSrcSlide] = timbre[i];
         src[kSrcLoop] = std::min(1.0f, breath[i]);
         src[kSrcKnot] = 0.45f;
         src[kSrcFold] = 0.2f;
         src[kSrcRegister] = (kNotes[i] - 43) / 60.0f;
         src[kSrcVapor] = 0.35f;
         src[kSrcDraft] = -0.3f;
         src[kSrcSwell] = 0.62f;
         src[kSrcLfo1] = 0.4f;
         src[kSrcLfo2] = -0.2f;
         out[i].offsets[destinationOf(kFine)] = 0.02f;
         out[i].offsets[destinationOf(kEmbouchure)] = 0.1f;
      }
   }
   // A clarinet-like tone (strong odd harmonics, a little even) at each voice's pitch.
   void readVoice(int slot, float *out, size_t count) override {
      if (slot >= 4) {
         for (size_t i = 0; i < count; ++i)
            out[i] = 0.0f;
         return;
      }
      const double hz = 440.0 * std::pow(2.0, (kNotes[slot] - 69) / 12.0);
      const double amp[4] = {0.3, 0.35, 0.45, 0.15};
      for (size_t i = 0; i < count; ++i) {
         const double ph = 2.0 * 3.141592653589793 * hz * static_cast<double>(i) / 48000.0;
         out[i] = static_cast<float>(amp[slot] * (std::sin(ph) + 0.12 * std::sin(2 * ph + 0.4) + 0.55 * std::sin(3 * ph + 0.9) +
                                                  0.25 * std::sin(5 * ph + 1.7) + 0.12 * std::sin(7 * ph + 0.3)));
      }
   }
   float outputPeak() override { return 0.42f; }
   double values_[kParamCount] = {};
   double sampleRate() const override { return 48000.0; }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height [frames]]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1648;
   const int height = argc > 3 ? std::atoi(argv[3]) : 940;
   const int frames = argc > 4 ? std::atoi(argv[4]) : 150;
   ScreenshotOptions options;
   options.playerTab = argc > 5 ? std::atoi(argv[5]) : 0;
   options.routes = argc > 6 && std::atoi(argv[6]) != 0;
   options.armed = argc > 7 ? std::atoi(argv[7]) : 0;
   DemoHost host;
   return renderScreenshot(host, width, height, frames, argv[1], options) ? 0 : 1;
}
