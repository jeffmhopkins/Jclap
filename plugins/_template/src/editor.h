#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace plugin_template {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The loudest output sample since the last call, for the meter.
   virtual float outputPeak() = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace plugin_template
