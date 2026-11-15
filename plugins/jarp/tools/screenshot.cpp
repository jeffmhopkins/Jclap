// Renders JArp's panel offscreen to a PNG, for the README and visual checks.
//
//    jarp-screenshot out.png [width height] [dice] [long] [note]
//
// The panel shows the Pedal Climb preset playing a C major 7th held on four MPE channels, each finger
// pressing a little differently, a few steps into the pattern. With `dice` the dice card is open; with
// `long` the pattern is 48 steps (Pedal Climb three times over, climbing an octave), playing in its
// second 16, so the grid scrolls. With `note` the G on step 2 has lane values of its own (an octave
// up, three hits, softer, a coin toss) and is picked, so the lanes show its values.

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
   DemoHost(bool longPattern, bool ownNote) {
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : findPreset("Pedal Climb").values)
         values_[id] = v;
      bank_ = *factoryBank("Pedal Climb");
      // A second and third pattern, so the chain strip shows some lit.
      bank_.slots[1] = factoryBank("Root Fifth")->slots[0];
      bank_.slots[2] = factoryBank("Press Roll")->slots[0];
      // A few steps left to chance, so the CHANCE lane and the grid's tags show.
      bank_.slots[0].lanes[5].chance = 60;
      bank_.slots[0].lanes[12].chance = 40;
      bank_.slots[0].lanes[14].chance = 75;
      if (ownNote) {
         Pattern &p = bank_.slots[0];
         const int i = p.noteStarting(kPickRow, kPickStep);
         if (i >= 0) {
            p.notes[i].own = kOwnOctave | kOwnRatchet | kOwnVelocity | kOwnChance;
            p.notes[i].lane = {1, 3, 0, -24, 50};
         }
      }
      double until = 10.2;
      if (longPattern) {
         const Pattern one = bank_.slots[0];
         Pattern p;
         p.length = 48;
         for (int k = 0; k < 3; ++k) {
            for (int i = 0; i < one.count; ++i) {
               const Note &n = one.notes[i];
               p.add(n.row, n.step + 16 * k, n.row == 0 && n.step == 0 ? kQuarters * 16 : n.length);
            }
            for (int st = 0; st < 16; ++st) {
               p.lanes[16 * k + st] = one.lanes[st];
               if (k == 2 && st >= 8)
                  p.lanes[16 * k + st].octave = static_cast<int8_t>(std::min(1, p.lanes[16 * k + st].octave + 1));
            }
         }
         bank_.slots[0] = p;
         until = 21.4;
      }
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
         if (b.beats >= until * 0.25)
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

   static constexpr int kPickRow = 2, kPickStep = 1;

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
   const bool sized = argc > 3 && std::atoi(argv[2]) > 0;
   const int width = sized ? std::atoi(argv[2]) : kEditorWidth;
   const int height = sized ? std::atoi(argv[3]) : kEditorHeight;
   bool dice = false, longPattern = false, ownNote = false;
   for (int i = 2; i < argc; ++i) {
      dice = dice || !std::strcmp(argv[i], "dice");
      longPattern = longPattern || !std::strcmp(argv[i], "long");
      ownNote = ownNote || !std::strcmp(argv[i], "note");
   }
   DemoHost host(longPattern, ownNote);
   return renderScreenshot(host, width, height, argv[1], dice, ownNote ? DemoHost::kPickRow : -1, DemoHost::kPickStep) ? 0 : 1;
}
