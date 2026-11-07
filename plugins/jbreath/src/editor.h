#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

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
bool renderScreenshot(EditorHost &host, int width, int height, int frames, const char *pngPath);

} // namespace jbreath
