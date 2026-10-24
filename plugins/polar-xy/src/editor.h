#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>

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
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace polar_xy
