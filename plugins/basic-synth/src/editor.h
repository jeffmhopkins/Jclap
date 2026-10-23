#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace basic_synth {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The newest output samples, oldest first, for the scope.
   virtual void readScope(float *out, size_t count) = 0;

   // The on-screen keyboard and note grid play the synth with these. Bend is in semitones;
   // pressure and timbre are 0..1, as in MPE.
   virtual void noteOn(int note, float velocity) = 0;
   virtual void noteExpression(int note, float bend, float pressure, float timbre) = 0;
   virtual void noteOff(int note) = 0;

   // The held voices, from the host or the panel, for the keyboard and note grid.
   struct Voice {
      int note;
      float bend, pressure, timbre;
   };
   virtual void voices(std::vector<Voice> &out) const = 0;
   // The newest held voice's LFO, -1..1, for the LFO light. False when no note is held.
   virtual bool lfo(float &value) const = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size, showing play page `page` (0 keys, 1 grid), and
// saves it as a PNG.
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath);

} // namespace basic_synth
