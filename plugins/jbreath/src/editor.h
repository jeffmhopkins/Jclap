#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "params.h"

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
   // The output's peak since the panel last asked, linear.
   virtual float outputPeak() = 0;
   // The name of the preset last loaded or saved, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG. `frames` steps the
// breath loops' animation that many timer ticks first.
// `options` opens a view first: a tab of the Player card (0 PLAYER, 1 LFOS, 2 BUSES), the ROUTES
// list in place of the loops, and MAP with a source armed (a ModSource; 0 for MAP off).
struct ScreenshotOptions {
   int playerTab = 0;
   bool routes = false;
   int armed = 0;
};
bool renderScreenshot(EditorHost &host, int width, int height, int frames, const char *pngPath,
                      const ScreenshotOptions &options = {});

} // namespace jbreath
