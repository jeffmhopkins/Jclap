#pragma once

// JComp's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "generator.h"
#include "shared.h"

namespace jcomp {

constexpr int kEditorWidth = 1000;
constexpr int kEditorHeight = 640;

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // The newest view of the engine.
   virtual const Frame &frame() = 0;
   // The song, as the panel edits it; a change goes to the audio thread at its next block.
   virtual const Song &song() const = 0;
   virtual void setSong(const Song &s) = 0;
   // A section note's action, as if played (the held ones toggle).
   virtual void request(int action) = 0;
   // Learn: arm it, and read back the note that came in (-1 for none yet).
   virtual void armLearn(bool on) = 0;
   virtual int takeLearned() = 0;
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;

   // The panel's own state, kept while it is closed.
   int page = 0;           // GENERATE, CHORDS, EDIT, PLAY
   int mutateSection = kChorus;
   int chordSection = kChorus, chordSlot = 0; // the chord picked in CHORDS (a slot: bar * 2 + half)
   int editSection = kChorus;
   int editTake = 0;       // 0..3, 4 the fill, 5 the fill down, 6 the ending
   int editStep = -1;      // the hit picked in EDIT
   bool playersOpen = false, browserOpen = false, notesOpen = false, formOpen = false;
   int learnAction = -1;   // the action waiting for a note in CUSTOM
   // The MUTATE card: the children on show, their tags, and the parents to go back to.
   std::vector<Pattern> litter;
   std::vector<std::string> tags;
   std::vector<Pattern> parents;
   // The song before each ROLL SONG, newest last.
   std::vector<Song> previous;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen on `page` (0 GENERATE, 1 CHORDS, 2 EDIT, 3 PLAY) and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath);

} // namespace jcomp
