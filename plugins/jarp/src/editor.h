#pragma once

// JArp's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>
#include <utility>
#include <vector>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "randomize.h"
#include "shared.h"

namespace jarp {

constexpr int kEditorWidth = 900;
constexpr int kEditorHeight = 743;

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The newest view of the fingers and the playhead.
   virtual const Frame &frame() = 0;
   // The pattern selected (the Pattern param), as the panel edits it; a change goes to the audio
   // thread at its next block.
   virtual const Pattern &pattern() const = 0;
   virtual void setPattern(const Pattern &p) = 0;
   // Every slot, and one slot or all of them replaced (paste, clear, a preset).
   virtual const Bank &bank() const = 0;
   virtual void setSlot(int slot, const Pattern &p) = 0;
   virtual void setBank(const Bank &b) = 0;
   // The name of the preset last loaded or saved, kept with the project.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
   // A line on that preset, shown in the header: a factory preset's own, or what the user wrote. Kept
   // with the project, and beside a user preset's file.
   virtual std::string presetBlurb() const = 0;
   virtual void setPresetBlurb(const std::string &blurb) = 0;

   // The dice card's settings and history, kept while the panel is closed.
   DiceSettings dice;
   DiceHistory diceHistory;
   bool diceOpen = false;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG, with the dice card open
// if `dice`.
// The notes starting on each (row, step) of `picks` are picked, as right- and shift-clicks would.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, bool dice = false,
                      const std::vector<std::pair<int, int>> &picks = {});

} // namespace jarp
