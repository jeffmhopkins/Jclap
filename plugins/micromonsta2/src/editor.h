#pragma once

// The plugin's knob panel, built from jclap::ui widgets. Kept behind this small interface so
// plugin.cpp never includes Visage.

#include <memory>

#include "jclap/ui/window.h"

namespace mm2 {

// What the panel needs from the plugin. Called on the main (GUI) thread.
class EditorHost {
public:
   virtual ~EditorHost() = default;
   // The knob's own setting for kCcParams[index].
   virtual double baseValue(int index) const = 0;
   // What is actually sent: the setting plus any host modulation.
   virtual double modulatedValue(int index) const = 0;
   virtual void beginEdit(int index) = 0;
   virtual void setValue(int index, double value) = 0;
   virtual void endEdit(int index) = 0;
   virtual int channel() const = 0; // 1-16
   virtual void setChannel(int channel) = 0;
   virtual void sendAll() = 0;
};

// The plugin window with the panel in it.
std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace mm2
