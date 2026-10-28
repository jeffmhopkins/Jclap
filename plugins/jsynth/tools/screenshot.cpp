// Renders the panel to a PNG without a host:
//    jsynth-screenshot out.png [part [routes [map-source [width height [view [voice-tab [env-tab [osc-tab [preset]]]]]]]]]
// Part 0 is A, 1 is B. Routes 1 opens the route list. Map-source, a route source's number, turns MAP
// on with that source armed. View 0 is the synth, 1 only the sky. Voice-tab is the one under the
// sky: 0 VOICE, 1 FLOCK, 2 MOTION, 3 TOUCH. Env-tab is 0 ENVELOPES, 1 LFOS, 2 BUSES; osc-tab is 0
// OSCILLATORS, 1 FM. Preset, a factory preset's name, loads it in place of the demo patch.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "editor.h"
#include "params.h"
#include "presets.h"

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
      // A few more routes, so the knobs show rings: Turn into the 2 > 1 FM, Heading pans.
      a(routeParam(6, kRouteSource), kSourceTurn);
      a(routeParam(6, kRouteDestination), destinationIndex(fmCell(1, 0)));
      a(routeParam(6, kRouteAmount), -0.2);
      a(routeParam(7, kRouteSource), kSourceHeading);
      a(routeParam(7, kRouteDestination), destinationIndex(kPartPan));
      a(routeParam(7, kRouteAmount), 0.4);
      a(routeParam(8, kRouteSource), kSourceVelocity + 3);
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
      // Part B's Env 2 and 3 in Shape mode, chained: a slow swell that hands on to stepping gates.
      b(envParam(1, kEnvMode), kModeShape);
      b(shapeParam(1, kShapeIndex), 13.0);
      b(shapeParam(1, kShapeTime), shapeValue(8000.0));
      b(shapeParam(1, kShapeWarp), 0.3);
      b(envParam(2, kEnvMode), kModeShape);
      b(shapeParam(2, kShapeIndex), 33.0);
      b(shapeParam(2, kShapeSync), 1.0);
      b(shapeParam(2, kShapeDivision), 5.0);
      b(shapeParam(2, kShapeRepeat), 3.0);
      b(shapeParam(2, kShapePhase), 0.6);
      b(chainParam(kChain), 5.0);
   }

   // A factory preset instead: its values, every other param at its default, and its bus names.
   explicit DemoHost(const Preset &preset) : name_(preset.name), names_(preset.buses) {
      const std::vector<double> v = presetValues(preset);
      for (size_t i = 0; i < paramCount(); ++i)
         values_[kParams()[i].def.id] = v[i];
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
   std::string presetName() const override { return name_; }
   void setPresetName(const std::string &) override {}
   BusNames busNames() const override { return names_; }
   void setBusNames(const BusNames &names) override { names_ = names; }
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
   std::string name_ = "Starling Bells";
   BusNames names_{{{"Breath Swell", "", "Late Shimmer", ""}, {}}};
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [part [routes [map-source [width height [view [voice-tab [env-tab [osc-tab "
                   "[preset]]]]]]]]]\n",
                   argv[0]);
      return 1;
   }
   const auto arg = [&](int i) { return argc > i ? std::atoi(argv[i]) : 0; };
   ScreenshotView view;
   view.part = arg(2);
   view.routes = arg(3) != 0;
   view.mapSource = arg(4);
   view.view = arg(7);
   view.voiceTab = arg(8);
   view.envTab = arg(9);
   view.oscTab = arg(10);
   const int width = argc > 6 ? arg(5) : 1648;
   const int height = argc > 6 ? arg(6) : 940;
   if (argc > 11) {
      for (const Preset &p : factoryPresets()) {
         if (p.name == argv[11]) {
            DemoHost host(p);
            return renderScreenshot(host, view, 6.0, width, height, argv[1]) ? 0 : 1;
         }
      }
      std::fprintf(stderr, "no factory preset called %s\n", argv[11]);
      return 1;
   }
   DemoHost host;
   return renderScreenshot(host, view, 6.0, width, height, argv[1]) ? 0 : 1;
}
