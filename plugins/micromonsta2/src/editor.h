#pragma once

// The plugin's knob panel, built from jclap::ui widgets. Kept behind this small interface so
// plugin.cpp never includes Visage.

#include <memory>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace mm2 {

// What the panel needs from the plugin, on the main (GUI) thread. Params are addressed by CLAP id:
// the CC number, or kChannelParamId. paramModulated() is what is actually sent, with host modulation.
class EditorHost : public jclap::ui::ParamAccess {
public:
   virtual int channel() const = 0; // 1-16
   virtual void setChannel(int channel) = 0;
   virtual void sendAll() = 0;
};

// The plugin window with the panel in it.
std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace mm2
