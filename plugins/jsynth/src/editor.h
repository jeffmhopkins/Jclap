#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "flock.h"

namespace jsynth {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // Clicking in the sky plays a note with these, and dragging steers it. Bend is in semitones;
   // pressure and timbre are 0..1, as in MPE.
   virtual void noteOn(int note, float velocity) = 0;
   virtual void noteExpression(int note, float bend, float pressure, float timbre) = 0;
   virtual void noteOff(int note) = 0;

   // The held notes, from the host or the panel: each is a creature's lure. Strike is the note-on
   // velocity and lift the release velocity (0 while held).
   struct Note {
      int note;
      float bend, pressure, timbre;
      float strike, lift;
   };
   virtual void notes(std::vector<Note> &out) const = 0;

   // The name of the preset last loaded or saved, kept with the plugin's state.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
   // Sets every param at once, as loading a preset does: values[i] is kParams()[i]'s. Each param that
   // changes is one edit for the host. The plugin coalesces these, so clicking quickly through
   // presets sends the host the latest values instead of a backlog that could overflow.
   virtual void setParams(const double *values);
   // The engine's flock as it last published it, or false while the plugin isn't processing (the
   // panel then flies its own).
   virtual bool flockFrame(FlockFrame &) const { return false; }

   // Where the panel was left, so reopening it shows the same view: the part (A or B), the left and
   // right cards' tabs, and whether the route list is open. Main thread only.
   int part = 0;
   int leftTab = 0;
   int rightTab = 0;
   bool routesOpen = false;
   // Whether the cards are hidden, leaving only the sky.
   bool panelsHidden = false;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// What a screenshot shows: the part, the cards' tabs, the route list, and MAP mode with a source
// armed (a Source from params.h, or 0 for MAP off), and whether the cards are hidden.
struct ScreenshotView {
   int part = 0;
   int leftTab = 0;
   int rightTab = 0;
   bool routes = false;
   int mapSource = 0;
   bool hidden = false;
};

// Flies the flock for `seconds`, then draws the panel offscreen at the given pixel size and saves it
// as a PNG.
bool renderScreenshot(EditorHost &host, const ScreenshotView &view, double seconds, int width, int height,
                      const char *pngPath);

} // namespace jsynth
