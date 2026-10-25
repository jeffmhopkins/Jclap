#pragma once

// A small piano roll: a bar's worth of steps across, pitches up the side, notes as bars with their
// names, and a playhead. For showing what a sequencer or note generator is about to play. Feed it
// from a timer with setNotes() and setPlayhead(); it only shows, it doesn't edit.
//
//    roll.setSteps(16);
//    roll.setNotes({{0, 3, 68, 0.8f}, {3, 3, 68, 0.9f}, {6, 2, 61, 0.7f}});
//    roll.setPlayhead(4.5f);

#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class NoteRoll : public visage::Frame {
public:
   struct Note {
      // In steps from the left edge.
      float start = 0.0f, length = 1.0f;
      int pitch = 60;
      // 0..1; quieter notes are drawn fainter.
      float velocity = 1.0f;
   };

   NoteRoll() { setIgnoresMouseEvents(true, false); }

   // How many steps the roll spans, and how many make a beat (a brighter grid line). 16 and 4 by default.
   void setSteps(int steps, int stepsPerBeat = 4);
   void setNotes(const std::vector<Note> &notes);
   // The playhead in steps; negative hides it. Notes under it are lit.
   void setPlayhead(float step);
   // The lowest and highest pitch shown. Without it the roll fits the notes, two semitones either side.
   void setPitchRange(int low, int high);
   // Shows a lane per distinct pitch instead of every semitone, so a few wide-apart notes stay readable.
   void setCompact(bool compact);
   // Shows exactly these pitches as lanes, highest at the top, whatever the notes are (a sequencer's
   // rows, say). An empty list goes back to the range or compact lanes.
   void setLanes(std::vector<int> pitches);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);
   void draw(visage::Canvas &canvas) override;

   // The pitch lanes from top to bottom, as last drawn. For tests and for labelling alongside.
   std::vector<int> lanes() const;

private:
   const Theme *theme_ = &defaultTheme();
   std::vector<Note> notes_;
   int steps_ = 16, stepsPerBeat_ = 4;
   float playhead_ = -1.0f;
   int low_ = 0, high_ = -1;
   bool compact_ = false;
   std::vector<int> fixedLanes_;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
