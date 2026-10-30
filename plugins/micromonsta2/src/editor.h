#pragma once

// The plugin's knob panel, built from jclap::ui widgets. Kept behind this small interface so
// plugin.cpp never includes Visage.

#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "patches.h"

namespace mm2 {

// What the panel needs from the plugin, on the main (GUI) thread. Params are addressed by CLAP id:
// the CC number, or kChannelParamId. paramModulated() is what is actually sent, with host modulation.
class EditorHost : public jclap::ui::ParamAccess {
public:
   virtual int channel() const = 0; // 1-16
   virtual void setChannel(int channel) = 0;
   virtual void sendAll() = 0;

   // Has the synth load `patch` (Bank Select + Program Change) and moves the knobs to its values
   // without sending them, since the synth already has them.
   virtual void loadProgram(const HardwarePatch &patch) = 0;
   // Moves the knobs and the routing to a sound the synth already has (read off one of its parts),
   // sending nothing. `patch.program` is the program it came from if that's known, otherwise 0.
   virtual void loadSound(const HardwarePatch &patch) = 0;
   // The program last loaded (1-300), or 0 if none has been.
   virtual int program() const = 0;
   // Whether route() is known: a program or a sound read off the synth has been loaded.
   virtual bool routingKnown() const = 0;
   // The loaded program's routing for mod matrix slot 0-9 (all "off" until one is known).
   virtual ModRoute route(int slot) const = 0;
   // The name the preset bar shows, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

// The plugin window with the panel in it.
std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace mm2
