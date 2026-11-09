#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "params.h"
#include "randomize.h"

namespace jbreath {

constexpr int kVoiceCount = 8;

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // Each voice, by slot: the note it plays (-1 when free), the pitch sounding, the breath blowing
   // (0..about 1.3), its MPE pressure and slide, a rough loudness, and whether it's been let go.
   struct Voice {
      int note = -1;
      float hz = 0.0f, breath = 0.0f, pressure = 0.0f, timbre = 0.5f, level = 0.0f;
      // The syllable of its words the Alien is singing (see sungWords), or -1.
      int saying = -1;
      bool releasing = false;
      // Its modulation sources now, and each destination's offset in its param's 0..1 range.
      std::array<float, kModSourceCount> sources{};
      std::array<float, kModDestCount> offsets{};
   };
   virtual void voices(std::vector<Voice> &out) const = 0;
   // A voice's newest output samples, oldest first, at the host's rate: what the breath loops draw.
   virtual void readVoice(int slot, float *out, size_t count) = 0;
   // The host's sample rate, which readVoice's samples are at.
   virtual double sampleRate() const = 0;
   // A channel's output peak (0 left, 1 right) since the panel last asked, linear.
   virtual float outputPeak(int channel) = 0;
   // The name of the preset last loaded or saved, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
   // The words typed for the Alien to sing, kept with the project.
   virtual std::string lyrics() const = 0;
   virtual void setLyrics(const std::string &text) = 0;
   // What the Alien sings now, written out (the typed words, or its own), and where each syllable's
   // word is in it, in the order it sings them.
   virtual void sungWords(std::string &text, std::vector<std::pair<int, int>> &syllables) const = 0;

   // The dice card: whether it is open, Randomize's complexity, Mutate's amount and the sections
   // both leave alone, and the patches they replaced, so UNDO can bring one back. Kept with the
   // plugin while the project is open (as JSynth's and JVerb's are), not with presets. Main thread only.
   bool diceOpen = false;
   // Whether the AIR view's VAPOR / LOOPS / PLACE panel is tucked away, leaving the window to the loops.
   bool airPanelHidden = false;
   DiceSettings dice;
   DiceHistory diceHistory;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG. `frames` steps the
// breath loops' animation that many timer ticks first.
// `options` opens a view first: a tab of the Player card (0 PLAYER, 1 VIBRATO, 2 LFOS, 3 BUSES),
// of the Exciter (0 EXCITER, 1 HYBRID) and of the Voice card (0 VOICE, 1 AIR, 2 ROOM), the ROUTES
// list in place of the loops, MAP with a source armed (a ModSource; 0 for MAP off), the AIR view
// in place of the panel, and the dice card.
struct ScreenshotOptions {
   int playerTab = 0;
   bool routes = false;
   int armed = 0;
   int exciterTab = 0;
   int voiceTab = 0;
   bool air = false;
   bool dice = false; // the dice card open
};
bool renderScreenshot(EditorHost &host, int width, int height, int frames, const char *pngPath,
                      const ScreenshotOptions &options = {});

// Times the panel offscreen over `frames` frames at 60 a second: its step (reading the voices,
// moving the smoke) and its drawing, in ms a frame over the second half, once the smoke has built.
struct PanelBenchmark {
   double stepMs = 0.0, drawMs = 0.0;
   int cells = 0; // the smoke grid's
};
PanelBenchmark benchmarkPanel(EditorHost &host, int width, int height, int frames, bool air);

} // namespace jbreath
