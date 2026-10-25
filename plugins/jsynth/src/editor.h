#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace jsynth {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The on-screen keyboard and note grid play notes with these. Bend is in semitones; pressure and
   // timbre are 0..1, as in MPE.
   virtual void noteOn(int note, float velocity) = 0;
   virtual void noteExpression(int note, float bend, float pressure, float timbre) = 0;
   virtual void noteOff(int note) = 0;

   // The held notes, from the host or the panel, for the keyboard, the note grid and the touch
   // meters. Strike is the note-on velocity and lift the release velocity (0 while held).
   struct Note {
      int note;
      float bend, pressure, timbre;
      float strike, lift;
   };
   virtual void notes(std::vector<Note> &out) const = 0;

   // Where the panel was left, so reopening it shows the same view: the page (main, mod, play), the
   // part (A or B), octaves up or down, and the grid's row interval in semitones. Main thread only.
   int page = 0;
   int part = 0;
   int playOctave = 0;
   int gridInterval = 5;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size, showing `page` (0 main, 1 mod, 2 play) for part
// `part`, and saves it as a PNG. `playPage` picks the keyboard (0) or the note grid (1).
bool renderScreenshot(EditorHost &host, int page, int part, int playPage, int width, int height, const char *pngPath);

} // namespace jsynth
