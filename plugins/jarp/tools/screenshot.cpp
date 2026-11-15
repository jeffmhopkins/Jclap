// Renders JArp's panel offscreen to a PNG, for the README and visual checks.
//
//    jarp-screenshot out.png [width height] [dice]
//
// The panel shows the Pedal Climb preset playing a C major 7th held on four MPE channels, each finger
// pressing a little differently, a few steps into the pattern. With `dice` the dice card is open.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace jarp;

class DemoHost : public EditorHost {
public:
   DemoHost() {
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : findPreset("Pedal Climb").values)
         values_[id] = v;
      bank_ = *factoryBank("Pedal Climb");
      // A second and third pattern, so the chain strip shows some lit.
      bank_.slots[1] = factoryBank("Root Fifth")->slots[0];
      bank_.slots[2] = factoryBank("Press Roll")->slots[0];
      Settings s;
      s.rate = static_cast<int>(values_[kRate]);
      s.swing = values_[kSwing];
      s.pressVelocity = values_[kPressVelocity];
      engine_.setSettings(s);
      engine_.setBank(bank_);

      // A Cmaj7 on channels 2-5, then play on to a little way into step 11.
      OutBuffer out;
      const int keys[] = {48, 52, 55, 59};
      const double pressure[] = {0.55, 0.3, 0.8, 0.45}, slide[] = {0.4, 0.6, 0.2, 0.7}, bend[] = {0.0, 0.1, -0.05, 0.0};
      Block b;
      b.frames = 480;
      b.sampleRate = 48000.0;
      b.hasTransport = true;
      b.playing = true;
      b.tempo = 120.0;
      for (int i = 0; i < 300; ++i) {
         b.beats = i * 480.0 / 48000.0 * 2.0;
         out.clear();
         engine_.beginBlock(b, out);
         if (i == 0)
            for (int f = 0; f < 4; ++f) {
               engine_.setPressure(f + 1, -1, pressure[f]);
               engine_.setSlide(f + 1, -1, slide[f]);
               engine_.setBend(f + 1, -1, bend[f]);
               engine_.noteOn(0, f + 1, keys[f], 100, out);
            }
         engine_.advance(b.frames, out);
         if (b.beats >= 10.2 * 0.25)
            break;
      }
      engine_.fillFrame(frame_);
   }

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}
   const Frame &frame() override { return frame_; }
   const Pattern &pattern() const override { return bank_.slots[slotIndex(values_[kSlot])]; }
   void setPattern(const Pattern &p) override { bank_.slots[slotIndex(values_[kSlot])] = p; }
   const Bank &bank() const override { return bank_; }
   void setSlot(int slot, const Pattern &p) override { bank_.slots[slot] = p; }
   void setBank(const Bank &b) override { bank_ = b; }
   std::string presetName() const override { return "Pedal Climb"; }
   void setPresetName(const std::string &) override {}

private:
   static const Preset &findPreset(const char *name) {
      for (const Preset &p : factoryPresets())
         if (p.name == name)
            return p;
      return factoryPresets().front();
   }

   double values_[kParamCount];
   Bank bank_;
   Engine engine_;
   Frame frame_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height] [dice]\n", argv[0]);
      return 2;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : kEditorWidth;
   const int height = argc > 3 ? std::atoi(argv[3]) : kEditorHeight;
   bool dice = false;
   for (int i = 2; i < argc; ++i)
      dice = dice || !std::strcmp(argv[i], "dice");
   DemoHost host;
   return renderScreenshot(host, width, height, argv[1], dice) ? 0 : 1;
}
