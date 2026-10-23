#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace polar_xy {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The last X/Y CC values received (0..127), for the live display. 64/64 until the first one.
   virtual void lastInput(int &x, int &y) const = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace polar_xy
