// Renders the panel to a PNG without a host:
//    jsynth-screenshot out.png [part [left-tab [right-tab [routes [map-source [width height]]]]]]
// Part 0 is A, 1 is B. The left card's tabs are FLOCK, FM, TOUCH and the right card's ENV, MOTION,
// LFO, FILTER (0 up). Routes 1 opens the route list. Map-source, a route source's number, turns MAP on
// with that source armed.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "editor.h"
#include "params.h"

namespace {

using namespace jsynth;

// A patch with something in each section: part A a bell-ish FM flock, part B a hunting saw bass,
// layered.
class DemoHost : public EditorHost {
public:
   DemoHost() {
      const auto a = [this](clap_id offset, double v) { values_[partId(0, offset)] = v; };
      const auto b = [this](clap_id offset, double v) { values_[partId(1, offset)] = v; };
      values_[kPlayMode] = 1.0;
      values_[kTouchCurve1 + 1] = -0.3;
      values_[kGlideRounding] = 0.4;

      a(oscParam(0, kOscWave), 0.0);
      a(oscParam(1, kOscWave), 5.0);
      a(oscParam(1, kOscRatio), 2.0);
      a(oscParam(2, kOscWave), 4.0);
      a(oscParam(2, kOscShape), 0.3);
      a(oscParam(2, kOscPitchMode), 0.0);
      a(oscParam(2, kOscCoarse), -12.0);
      a(fmCell(1, 0), 0.45);
      a(fmCell(1, 1), 0.2);
      a(fmCell(2, 0), 0.1);
      a(mixParam(kMixLevel, 2), 0.4);
      a(mixParam(kMixPan, 2), 0.3);
      a(mixParam(kNoiseLevel), 0.1);
      a(filterParam(0, kFilterCutoff), 0.55);
      a(filterParam(0, kFilterResonance), 0.35);
      a(filterParam(0, kFilterKeyTrack), 0.5);
      a(envParam(0, kEnvDecay), timeValue(900.0));
      a(envParam(0, kEnvSustain), 0.4);
      a(envParam(1, kEnvAttack), timeValue(1.0));
      a(envParam(1, kEnvDecay), timeValue(250.0));
      a(envParam(2, kEnvLoop), 1.0);
      a(flockParam(kNeighbourFm), 0.5);
      a(flockParam(kTunePull), 0.3);
      a(flockParam(kTailLength), 0.6);
      a(flockParam(kTailSpread), 0.6);
      // A few more routes, so the knobs show rings: Speed and Wake into the 1 > 2 FM, Heading pans.
      a(routeParam(6, kRouteSource), kSourceWake);
      a(routeParam(6, kRouteDestination), destinationIndex(fmCell(1, 0)));
      a(routeParam(6, kRouteAmount), -0.2);
      a(routeParam(7, kRouteSource), kSourceHeading);
      a(routeParam(7, kRouteDestination), destinationIndex(kPartPan));
      a(routeParam(7, kRouteAmount), 0.4);
      a(routeParam(8, kRouteSource), kSourceStrike + 3);
      a(routeParam(8, kRouteDestination), destinationIndex(oscParam(1, kOscShape)));
      a(routeParam(8, kRouteAmount), 0.3);

      b(kPartLevel, -4.0);
      b(kPolyphony, 4.0);
      b(kVoiceMode, 0.0);
      b(kGlideTime, timeValue(80.0));
      b(oscParam(0, kOscWave), 2.0);
      b(oscParam(1, kOscWave), 2.0);
      b(oscParam(1, kOscPitchMode), 0.0);
      b(oscParam(1, kOscFine), -12.0);
      b(mixParam(kMixLevel, 1), 0.8);
      b(tailParam(kFilterRouting), 1.0);
      b(filterParam(0, kFilterCutoff), 0.45);
      b(filterParam(1, kFilterType), 2.0);
      b(filterParam(1, kFilterCutoff), 0.65);
      b(envParam(0, kEnvAttack), timeValue(20.0));
      b(envParam(0, kEnvRelease), timeValue(900.0));
      b(flockParam(kSpeed), 0.7);
      b(flockParam(kTailLength), 0.35);
      b(flockParam(kTailSpread), 0.3);
   }

   double paramValue(clap_id id) const override {
      const auto it = values_.find(id);
      if (it != values_.end())
         return it->second;
      const Param *p = findParam(id);
      return p ? p->def.defaultValue : 0.0;
   }
   double paramModulated(clap_id id) const override { return paramValue(id); }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override { values_[id] = v; }
   void endEdit(clap_id) override {}
   void noteOn(int, float) override {}
   void noteExpression(int, float, float, float) override {}
   void noteOff(int) override {}
   std::string presetName() const override { return "Starling Bells"; }
   void setPresetName(const std::string &) override {}
   void notes(std::vector<Note> &out) const override {
      // A chord, each note bent and pressed a little differently, as MPE playing looks.
      out = {{43, -0.4f, 0.3f, 0.35f, 0.6f, 0.0f},
             {55, 0.0f, 0.6f, 0.7f, 0.7f, 0.0f},
             {62, 0.9f, 0.85f, 0.45f, 0.5f, 0.0f},
             {67, 0.2f, 0.5f, 0.6f, 0.8f, 0.0f},
             {74, -0.3f, 0.4f, 0.3f, 0.6f, 0.0f}};
   }

private:
   std::map<clap_id, double> values_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [part [left-tab [right-tab [routes [map-source [width height [hidden]]]]]]]\n",
                   argv[0]);
      return 1;
   }
   const auto arg = [&](int i) { return argc > i ? std::atoi(argv[i]) : 0; };
   ScreenshotView view;
   view.part = arg(2);
   view.leftTab = arg(3);
   view.rightTab = arg(4);
   view.routes = arg(5) != 0;
   view.mapSource = arg(6);
   view.hidden = arg(9) != 0;
   const int width = argc > 8 ? arg(7) : 1200;
   const int height = argc > 8 ? arg(8) : 780;
   DemoHost host;
   return renderScreenshot(host, view, 6.0, width, height, argv[1]) ? 0 : 1;
}
