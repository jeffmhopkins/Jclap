#pragma once

// Monolith's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "shared.h"

namespace monolith {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The newest view of the slab and the voice.
   virtual const SlabFrame &frame() = 0;
   // Jump, re-roll, carve or restore; applied by the audio thread at its next block.
   virtual void command(const Command &c) = 0;
   // The name of the preset last loaded or saved, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG. `selected` is the cell
// shown in the decode view (-1 follows the voice).
bool renderScreenshot(EditorHost &host, int width, int height, int selected, const char *pngPath);

} // namespace monolith
