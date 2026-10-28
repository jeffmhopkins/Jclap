#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstddef>
#include <cstdint>
#include <functional>
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

   // The held notes, from the host or the panel: each is a creature's lure. Velocity is the note-on
   // velocity and release the release velocity (0 while held).
   struct Note {
      int note;
      float bend, pressure, timbre;
      float velocity, release;
   };
   virtual void notes(std::vector<Note> &out) const = 0;

   // The name of the preset last loaded or saved, kept with the plugin's state.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
   // The buses' names, kept with the plugin's state and saved in user presets.
   virtual BusNames busNames() const { return {}; }
   virtual void setBusNames(const BusNames &) {}
   // Sets every param at once, as loading a preset does: values[i] is kParams()[i]'s. Each param that
   // changes is one edit for the host. The plugin coalesces these, so clicking quickly through
   // presets sends the host the latest values instead of a backlog that could overflow.
   virtual void setParams(const double *values);
   // The engine's flock as it last published it, or false while the plugin isn't processing (the
   // panel then flies its own).
   virtual bool flockFrame(FlockFrame &) const { return false; }

   // Where the panel was left, so reopening it shows the same view: the part (A or B), and whether
   // the route list is open. Main thread only.
   int part = 0;
   bool routesOpen = false;
   // The view: 0 the synth (its panels beside the sky), 1 only the sky.
   int view = 0;
   // Whether the sky draws the held notes' lure rings.
   bool ringsShown = true;
   // How the sky is drawn: its look (0 Sumi, 1 Aurora), and the palette each look was left in.
   // Unlike the above, these are kept with the plugin's state.
   int skyLook = 0;
   int skyPalettes[2] = {0, 1};
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// What a screenshot shows: the part, the route list, and MAP mode with a source
// armed (a Source from params.h, or 0 for MAP off), and the view (0 the synth, 1 sky only).
struct ScreenshotView {
   int part = 0;
   bool routes = false;
   int mapSource = 0;
   int view = 0;
   int voiceTab = 0; // the tab under the sky: 0 VOICE, 1 FLOCK, 2 MOTION, 3 TOUCH
   int envTab = 0;   // the envelope card's: 0 ENVELOPES, 1 LFOS, 2 BUSES
   int oscTab = 0;   // the oscillator card's: 0 OSCILLATORS, 1 FM
};

// Flies the flock for `seconds`, then draws the panel offscreen at the given pixel size and saves it
// as a PNG.
bool renderScreenshot(EditorHost &host, const ScreenshotView &view, double seconds, int width, int height,
                      const char *pngPath);

// Renders the panel as a run of frames, `fps` a second, offscreen at the given pixel size: before
// each frame `advance(frame)` runs (the caller moves its engine on by a frame's worth of audio), then
// the panel steps and draws, and `frame(rgba)` gets its pixels (width x height, 4 bytes each).
bool renderFrames(EditorHost &host, const ScreenshotView &view, int width, int height, int frames, double fps,
                  const std::function<void(int)> &advance, const std::function<void(const uint8_t *)> &frame);

} // namespace jsynth
