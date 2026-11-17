// Renders JArp's panel offscreen to a PNG, for the README and visual checks.
//
//    jarp-screenshot out.png [width height] [dice] [long] [note] [picks] [preset=NAME] [rows=N]
//                    [held] [bounce] [repeatends] [advance=N] [repeat=N] [then=N] [oneshot]
//                    [passsustain] [keys=K,K,...]
//
// The panel shows the Pedal Climb preset playing a C major 7th held on four MPE channels, each finger
// pressing a little differently, a few steps into the pattern. With `dice` the dice card is open; with
// `long` the pattern is 48 steps (Pedal Climb three times over, climbing an octave), playing in its
// second 16, so the grid scrolls. With `note` the G on step 2 has lane values of its own (an octave
// up, three hits, softer, a coin toss) and is picked, so the lanes show its values. With `picks` four
// notes are picked together, each given a chance of its own by one edit. For the manual: `preset=NAME`
// shows another factory preset, `rows=N` sets ROWS BY (0 Pitch, 1 Degree, 2 Held Degree, 3 Spread),
// `held`, `bounce` and `repeatends` set the pattern's LENGTH box, `advance=N` the chain switch,
// `repeat=N` and `then=N` (0 Next, 1 Random) the CHAIN card, `oneshot` and `passsustain` the PLAY
// card's boxes, and
// `keys=` the MIDI keys held (one finger each, channels 2 up).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace jarp;

// The manual's extras: another preset, ROWS BY, the LENGTH box and the chain switch.
struct Extras {
   std::string preset = "Pedal Climb";
   int rowsBy = -1, advance = -1, repeat = -1, then = -1;
   bool oneShot = false, passSustain = false;
   std::vector<int> keys = {48, 52, 55, 59};
   bool held = false, bounce = false, repeatEnds = false;
};

class DemoHost : public EditorHost {
public:
   DemoHost(bool longPattern, bool ownNote, bool picks = false, const Extras &x = {}) : name_(x.preset) {
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : findPreset(name_.c_str()).values)
         values_[id] = v;
      if (!factoryBank(name_))
         name_ = "Pedal Climb";
      bank_ = *factoryBank(name_);
      if (name_ == "Pedal Climb") {
         // A second and third pattern, so the chain strip shows some lit.
         bank_.slots[1] = factoryBank("Root Fifth")->slots[0];
         bank_.slots[2] = factoryBank("Press Roll")->slots[0];
         // A few steps left to chance, so the CHANCE lane and the grid's tags show.
         bank_.slots[0].lanes[5].chance = 60;
         bank_.slots[0].lanes[12].chance = 40;
         bank_.slots[0].lanes[14].chance = 75;
      }
      if (x.rowsBy >= 0)
         values_[kRowsBy] = x.rowsBy;
      if (x.advance >= 0)
         values_[kAdvance] = x.advance;
      if (x.repeat >= 0)
         values_[kRepeat] = x.repeat;
      if (x.then >= 0)
         values_[kOrder] = x.then;
      if (x.oneShot)
         values_[kOneShot] = 1;
      if (x.passSustain)
         values_[kPassSustain] = 1;
      if (x.held)
         bank_.slots[0].byHeld = 1;
      if (x.bounce)
         bank_.slots[0].bounce = 1;
      if (x.repeatEnds)
         bank_.slots[0].repeatEnds = 1;
      if (ownNote) {
         Pattern &p = bank_.slots[0];
         const int i = p.noteStarting(kPickRow, kPickStep);
         if (i >= 0) {
            p.notes[i].own = kOwnOctave | kOwnRatchet | kOwnVelocity | kOwnChance;
            p.notes[i].lane = {1, 3, 0, -24, 50};
         }
      }
      if (picks) {
         Pattern &p = bank_.slots[0];
         for (const auto &[row, step] : kPicks)
            if (const int i = p.noteStarting(row, step); i >= 0) {
               p.notes[i].own = kOwnChance;
               p.notes[i].lane.chance = 50;
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
      const auto whole = [this](ParamId id) { return static_cast<int>(std::lround(values_[id])); };
      Settings s;
      s.rate = whole(kRate);
      s.swing = values_[kSwing];
      s.advance = static_cast<Advance>(whole(kAdvance));
      s.latch = whole(kLatch) != 0;
      s.restart = static_cast<Restart>(whole(kRestart));
      s.degree = whole(kRowsBy) == 1 || whole(kRowsBy) == 2;
      s.strict = whole(kRowsBy) == 2;
      s.spread = whole(kRowsBy) == 3;
      s.wrap = whole(kEmptyRows) != 0;
      s.octaves = whole(kEmptyRows) == 2;
      s.key = whole(kKey);
      s.scale = whole(kScale);
      s.range = whole(kRange) + 1;
      s.keepRows = whole(kKeepRows) != 0;
      s.repeat = whole(kRepeat);
      s.order = static_cast<Order>(whole(kOrder));
      s.oneShot = whole(kOneShot) != 0;
      s.pressVelocity = values_[kPressVelocity];
      s.curve = values_[kCurve];
      s.pressRatchet = values_[kPressRatchet];
      s.maxHits = whole(kMaxHits);
      engine_.setSettings(s);
      engine_.setBank(bank_);

      // A Cmaj7 (or the extras' keys) on channels 2 up, then play on to a little way into step 11.
      OutBuffer out;
      const std::vector<int> &keys = x.keys;
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
            for (int f = 0; f < static_cast<int>(keys.size()) && f < 15; ++f) {
               engine_.setPressure(f + 1, -1, pressure[f % 4]);
               engine_.setSlide(f + 1, -1, slide[f % 4]);
               engine_.setBend(f + 1, -1, bend[f % 4]);
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
   std::string presetName() const override { return name_; }
   void setPresetName(const std::string &) override {}
   std::string presetBlurb() const override { return factoryBlurb(name_); }
   void setPresetBlurb(const std::string &) override {}

   static constexpr int kPickRow = 2, kPickStep = 1;
   static constexpr std::pair<int, int> kPicks[] = {{3, 2}, {1, 4}, {2, 5}, {3, 6}};

private:
   static const Preset &findPreset(const char *name) {
      for (const Preset &p : factoryPresets())
         if (p.name == name)
            return p;
      return factoryPresets().front();
   }

   std::string name_;
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
   bool dice = false, longPattern = false, ownNote = false, picks = false;
   Extras x;
   for (int i = 2; i < argc; ++i) {
      if (!std::strncmp(argv[i], "preset=", 7))
         x.preset = argv[i] + 7;
      else if (!std::strncmp(argv[i], "rows=", 5))
         x.rowsBy = std::atoi(argv[i] + 5);
      else if (!std::strncmp(argv[i], "keys=", 5)) {
         x.keys.clear();
         for (const char *k = argv[i] + 5; *k;) {
            x.keys.push_back(std::atoi(k));
            while (*k && *k != ',')
               ++k;
            if (*k)
               ++k;
         }
      }
      else if (!std::strncmp(argv[i], "repeat=", 7))
         x.repeat = std::atoi(argv[i] + 7);
      else if (!std::strncmp(argv[i], "then=", 5))
         x.then = std::atoi(argv[i] + 5);
      else if (!std::strncmp(argv[i], "advance=", 8))
         x.advance = std::atoi(argv[i] + 8);
      x.held = x.held || !std::strcmp(argv[i], "held");
      x.oneShot = x.oneShot || !std::strcmp(argv[i], "oneshot");
      x.passSustain = x.passSustain || !std::strcmp(argv[i], "passsustain");
      x.bounce = x.bounce || !std::strcmp(argv[i], "bounce");
      x.repeatEnds = x.repeatEnds || !std::strcmp(argv[i], "repeatends");
      dice = dice || !std::strcmp(argv[i], "dice");
      longPattern = longPattern || !std::strcmp(argv[i], "long");
      ownNote = ownNote || !std::strcmp(argv[i], "note");
      picks = picks || !std::strcmp(argv[i], "picks");
   }
   DemoHost host(longPattern, ownNote, picks, x);
   std::vector<std::pair<int, int>> pick;
   if (ownNote)
      pick.push_back({DemoHost::kPickRow, DemoHost::kPickStep});
   if (picks)
      pick.assign(std::begin(DemoHost::kPicks), std::end(DemoHost::kPicks));
   return renderScreenshot(host, width, height, argv[1], dice, pick) ? 0 : 1;
}
