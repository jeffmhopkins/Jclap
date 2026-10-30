#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <functional>
#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace polar_xy {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The last X/Y CC values received (0..127) and their MIDI channel (0..15), all from the same
   // channel, for the live display. 64/64 on channel 0 until the first one.
   virtual void lastInput(int &x, int &y, int &channel) const = 0;
   // What that channel last sent: its angle and radius CCs. False while it hasn't sent any (only one
   // axis has arrived), and then angle and radius are left as they were.
   virtual bool lastOutput(int &angle, int &radius) const = 0;
   // Where that channel's output is right now and the X/Y it is headed for, both on the -1..+1 pad
   // and between the CC steps, for drawing its glide. False while lastOutput is.
   virtual bool liveOutput(double &x, double &y, double &targetX, double &targetY) const = 0;
   // The XY IN pad dragged with the mouse: moves the shown channel to this X/Y (0..127, between the
   // CC steps too) as the controller would, and holds it there, ignoring that channel's X/Y CCs,
   // until released.
   virtual void dragInput(double x, double y) = 0;
   virtual void releaseInput() = 0;
   // Sends just one output CC (0: angle, 1: radius) on the shown channel, at its last value, so a
   // MIDI learn picks up that CC alone.
   virtual void sendOutput(int which) = 0;
   // The name of the preset last loaded or saved, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG. It is animated for
// `seconds` first, at 60 frames a second, calling `advance` with each frame's length before it, so
// the host can move the output and the panel's ink has a trail to show.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, double seconds = 0.0,
                      const std::function<void(double)> &advance = {});

} // namespace polar_xy
