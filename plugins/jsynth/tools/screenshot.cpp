// Renders the panel to a PNG without a host: jsynth-screenshot out.png [page [part [view [width height]]]]
// Page 0 is MAIN, 1 MOD, 2 PLAY; part 0 is A, 1 is B; view 0 shows the keyboard, 1 the note grid.

#include <cstdio>
#include <cstdlib>
#include <map>

#include "editor.h"
#include "params.h"

namespace {

using namespace jsynth;

// A patch with something in each section: part A an FM electric piano with a saw layer, part B a
// pad, layered.
class DemoHost : public EditorHost {
public:
   DemoHost() {
      const auto a = [this](clap_id offset, double v) { values_[partId(0, offset)] = v; };
      const auto b = [this](clap_id offset, double v) { values_[partId(1, offset)] = v; };
      values_[kPlayMode] = 1.0;
      values_[kMacro1] = 0.35;
      values_[kTouchCurve1 + 1] = -0.3;
      values_[kGlideRounding] = 0.4;

      a(oscParam(0, kOscWave), 0.0);
      a(oscParam(1, kOscWave), 5.0);
      a(oscParam(1, kOscPitchMode), 1.0);
      a(oscParam(1, kOscRatio), 2.0);
      a(oscParam(2, kOscWave), 4.0);
      a(oscParam(2, kOscShape), 0.3);
      a(oscParam(2, kOscCoarse), -12.0);
      a(oscParam(2, kOscBend), 0.5);
      a(fmCell(1, 0), 0.45);
      a(fmCell(1, 1), 0.2);
      a(fmCell(2, 0), 0.1);
      a(mixParam(kMixLevel, 1), 0.0);
      a(mixParam(kMixLevel, 2), 0.4);
      a(mixParam(kMixPan, 2), 0.3);
      a(mixParam(kNoiseLevel), 0.1);
      a(tailParam(kDriveAmount), 0.25);
      a(filterParam(0, kFilterCutoff), 0.55);
      a(filterParam(0, kFilterResonance), 0.35);
      a(filterParam(0, kFilterKeyTrack), 0.5);
      a(filterParam(1, kFilterCutoff), 0.85);
      a(tailParam(kShaperType), 2.0);
      a(tailParam(kShaperAmount), 0.3);
      a(envParam(0, kEnvDecay), timeValue(900.0));
      a(envParam(0, kEnvSustain), 0.4);
      a(envParam(1, kEnvAttack), timeValue(1.0));
      a(envParam(1, kEnvDecay), timeValue(250.0));
      a(envParam(2, kEnvLoop), 1.0);
      a(envParam(2, kEnvDecayCurve), -0.4);
      a(lfoParam(0, kLfoSync), 1.0);
      a(lfoParam(1, kLfoTrigger), 2.0);
      a(slotParam(5, kSlotSource), 10.0); // Lift
      a(slotParam(5, kSlotDestination), 0.0);
      a(slotParam(5, kSlotAmount), -0.25);
      a(slotParam(6, kSlotSource), 11.0); // Key, via Macro 1
      a(slotParam(6, kSlotVia), 18.0);
      a(slotParam(6, kSlotCurve), 3.0);
      a(slotParam(6, kSlotAmount), 0.5);

      b(kPartLevel, -4.0);
      b(kPartPan, -0.2);
      b(kPartTranspose, 12.0);
      b(kVoiceMode, 0.0);
      b(kPolyphony, 16.0);
      b(kGlideTime, timeValue(80.0));
      b(oscParam(0, kOscWave), 2.0);
      b(oscParam(1, kOscWave), 2.0);
      b(oscParam(1, kOscFine), -12.0);
      b(mixParam(kMixLevel, 1), 0.8);
      b(mixParam(kMixPan, 0), -0.6);
      b(mixParam(kMixPan, 1), 0.6);
      b(tailParam(kFilterRouting), 1.0);
      b(filterParam(0, kFilterCutoff), 0.45);
      b(filterParam(1, kFilterType), 2.0);
      b(filterParam(1, kFilterCutoff), 0.65);
      b(envParam(0, kEnvAttack), timeValue(600.0));
      b(envParam(0, kEnvRelease), timeValue(2500.0));
   }

   double paramValue(clap_id id) const override {
      const auto it = values_.find(id);
      if (it != values_.end())
         return it->second;
      const Param *p = findParam(id);
      return p ? p->def.defaultValue : 0.0;
   }
   // Some host modulation, to show how the knobs draw it.
   double paramModulated(clap_id id) const override {
      if (id == partId(0, filterParam(0, kFilterCutoff)))
         return 0.68;
      if (id == partId(0, fmCell(1, 0)))
         return 0.6;
      return paramValue(id);
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   void noteOn(int, float) override {}
   void noteExpression(int, float, float, float) override {}
   void noteOff(int) override {}
   void notes(std::vector<Note> &out) const override {
      // A chord, each note bent and pressed a little differently, as MPE playing looks.
      out = {{48, -0.4f, 0.3f, 0.5f, 0.6f, 0.0f},
             {55, 0.0f, 0.6f, 0.7f, 0.7f, 0.0f},
             {60, 0.9f, 0.85f, 0.3f, 0.5f, 0.0f},
             {64, 0.2f, 0.5f, 0.55f, 0.8f, 0.0f}};
   }

private:
   std::map<clap_id, double> values_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [page [part [view [width height]]]]\n", argv[0]);
      return 1;
   }
   const int page = argc > 2 ? std::atoi(argv[2]) : 0;
   const int part = argc > 3 ? std::atoi(argv[3]) : 0;
   const int view = argc > 4 ? std::atoi(argv[4]) : 0;
   const int width = argc > 6 ? std::atoi(argv[5]) : 1280;
   const int height = argc > 6 ? std::atoi(argv[6]) : 940;
   DemoHost host;
   return renderScreenshot(host, page, part, view, width, height, argv[1]) ? 0 : 1;
}
