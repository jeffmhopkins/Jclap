#pragma once

// The plugin's knob panel, built from jclap::ui widgets. Kept behind this small interface so
// plugin.cpp never includes Visage.

#include <cstdint>
#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace mb {

struct HardwarePatch;

// What the panel needs from the plugin, on the main (GUI) thread. Params are addressed by CLAP id:
// the CC number, or one of the settings ids. paramModulated() is what is actually sent, with host
// modulation.
class EditorHost : public jclap::ui::ParamAccess {
public:
   virtual int channel() const = 0; // 1-16
   virtual void setChannel(int channel) = 0;
   virtual void sendAll() = 0;

   // Whether the plugin knows the synth's value for this CC: something set it (the panel, the host,
   // a saved project) or the synth sent it. After a preset change on the synth nothing is known
   // until each knob moves.
   virtual bool known(int cc) const = 0;
   // Counts every CC that came in on the channel and moved a knob, for the activity light.
   virtual uint32_t heardCount() const = 0;
   // The preset the synth has loaded as far as the plugin knows (0-255, A01-P16): the last one the
   // plugin picked, or the last Bank Select and Program Change that came in. -1 if neither.
   virtual int synthPreset() const = 0;
   // Loads one of the synth's own presets: sends Bank Select and Program Change, and moves every knob
   // the preset stores to its value without sending it.
   virtual void loadProgram(const HardwarePatch &patch) = 0;

   // The name the preset bar shows, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

// The plugin window with the panel in it.
std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace mb
