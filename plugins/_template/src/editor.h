#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace plugin_template {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The loudest output sample since the last call, for the meter.
   virtual float outputPeak() = 0;
   // The newest output samples (left channel), oldest first, for the scope.
   virtual void readScope(float *out, size_t count) = 0;

   // The on-screen keyboard and note grid play the test tone with these. Bend is in semitones;
   // pressure and timbre are 0..1, as in MPE.
   virtual void noteOn(int note, float velocity) = 0;
   virtual void noteExpression(int note, float bend, float pressure, float timbre) = 0;
   virtual void noteOff(int note) = 0;
   // Whether a note is held, by the host or the keyboard, and how many note-ons have arrived so
   // far (the activity light flashes when it changes).
   virtual bool noteHeld(int note) const = 0;
   virtual uint32_t noteCount() const = 0;
   // The test tone's voice, for the note grid: false when it is silent.
   struct Voice {
      int note;
      float bend, pressure, timbre;
   };
   virtual bool voice(Voice &out) const = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size, showing tab `page`, and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath);

} // namespace plugin_template
