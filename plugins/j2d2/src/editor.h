#pragma once

// J2D2's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace j2d2 {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The loudest output sample on each side since the last call, for the meters.
   virtual void outputPeaks(float &left, float &right) = 0;
   // The newest output samples (mono), oldest first, for the spectrum.
   virtual void readScope(float *out, size_t count) = 0;

   // One playing voice, as the panel draws it: its note, where the Personality XY has it now
   // (energy, feeling), its pressure, how loud its droid and drone halves are, its place in the
   // field, and the droid sound it is making (an EventKind from engine.h, kEvNone when quiet) at
   // which pitch (a MIDI note).
   struct Voice {
      bool active = false;
      int note = -1;
      float energy = 0, feeling = 0, pressure = 0, droid = 0, drone = 0, pan = 0;
      int event = 10;
      float eventPitch = 0;
   };
   virtual Voice voice(int index) const = 0;
   // The two shape LFOs, -1..1, as they are now.
   virtual float lfo(int index) const = 0;

   // The preset showing in the header.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size, showing page `page` (0 VOICE, 1 DRONES), and
// saves it as a PNG.
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath);

} // namespace j2d2
