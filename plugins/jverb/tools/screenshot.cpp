// Renders the panel to a PNG without a host: jverb-screenshot out.png [bodies [width height]]
//
// A stand-in host runs the orbit sim itself and makes up a drum-and-pad input, so the render shows
// trails, gas and dust as they look with audio playing. Everything is seeded, so the same
// arguments always draw the same picture.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "orbit.h"
#include "params.h"

namespace {

using namespace jverb;

class DemoHost : public EditorHost {
public:
   explicit DemoHost(int bodies) {
      for (clap_id id = 0; id < kParamCount; ++id)
         values_[id] = kParams[id].defaultValue;
      values_[kBodies] = bodies;
      values_[kDecay] = 6.5;
      values_[kSize] = 1.6;
      values_[kMix] = 0.42;
      values_[kFeedback] = 0.9;
      values_[kShimmer] = 0.3;
      values_[kDriveOn] = 1.0;
      double mass[kMaxBodies], velocity[kMaxBodies];
      for (int b = 0; b < kMaxBodies; ++b) {
         mass[b] = values_[bodyParam(b, kBody1Mass)];
         velocity[b] = 1.0;
      }
      sim_.reset(bodies, mass, velocity, 1.0);
   }

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}

   // One panel frame (1/60 s) of orbits and made-up audio: a kick every half second, hats on the
   // eighths, and a pad that swells.
   bool readFrame(OrbitFrame &f) override {
      double mass[kMaxBodies];
      for (int b = 0; b < kMaxBodies; ++b)
         mass[b] = values_[bodyParam(b, kBody1Mass)];
      const double t = frame_++ / 60.0;
      sim_.step(0.8 / 60.0, mass, 1.0);
      f.count = sim_.count();
      for (int b = 0; b < kMaxBodies; ++b) {
         const OrbitSim::Body &body = sim_.body(b);
         f.x[b] = static_cast<float>(body.x);
         f.y[b] = static_cast<float>(body.y);
         f.vx[b] = static_cast<float>(body.vx);
         f.vy[b] = static_cast<float>(body.vy);
         f.mass[b] = static_cast<float>(mass[b]);
         f.delayMs[b] = static_cast<float>(1.6 * 40.0 * (1.0 + 0.4 * (sim_.radius(b) / 0.9 - 1.0)));
      }
      const double beat = std::fmod(t, 0.5) / 0.5, eighth = std::fmod(t, 0.25) / 0.25;
      f.band[0] = static_cast<float>(0.9 * std::exp(-6.0 * beat));
      f.band[1] = static_cast<float>(0.35 + 0.25 * std::sin(t * 0.7));
      f.band[2] = static_cast<float>(0.5 * std::exp(-14.0 * eighth));
      if (frame_ % 30 == 0) {
         ++f.hits[0];
         f.hitStrength[0] = 0.9f;
      }
      if (frame_ % 15 == 7) {
         ++f.hits[2];
         f.hitStrength[2] = 0.45f;
      }
      f.late = 0.25f;
      f.decay = static_cast<float>(values_[kDecay]);
      return true;
   }
   void send(const OrbitCommand &) override {}
   void outputPeaks(float &left, float &right) override {
      left = 0.52f;
      right = 0.47f;
   }
   std::string presetName() const override { return "Shimmer Halo"; }
   void setPresetName(const std::string &) override {}

private:
   double values_[kParamCount];
   OrbitSim sim_;
   long frame_ = 0;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [bodies [width height]]\n", argv[0]);
      return 1;
   }
   const int bodies = argc > 2 ? std::atoi(argv[2]) : 3;
   const int width = argc > 4 ? std::atoi(argv[3]) : 1200;
   const int height = argc > 4 ? std::atoi(argv[4]) : 780;
   DemoHost host(bodies < 2 ? 3 : bodies);
   return renderScreenshot(host, 7.0, width, height, argv[1]) ? 0 : 1;
}
