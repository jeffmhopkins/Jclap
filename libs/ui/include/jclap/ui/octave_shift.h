#pragma once

// Octave - and + buttons around a readout, for moving a PianoKeyboard or NoteGrid up and down:
//
//    auto &octave = add<OctaveShift>(bounds, -3, 3);
//    octave.valueText = [&](int) { return noteName(keys.lowNote()) + "-" + noteName(keys.highNote()); };
//    octave.onChange = [&](int, int step) { keys.shiftOctaves(step); };
//
// Click a button or scroll to step. A button dims at its end of the range.

#include <functional>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class OctaveShift : public visage::Frame {
public:
   explicit OctaveShift(int min = -3, int max = 3);

   void setRange(int min, int max);
   int value() const { return value_; }
   // Changes the value without calling onChange (for restoring it).
   void setValue(int value);

   // The readout; by default "OCT +1". Called on every draw, so it can show what the value moves.
   std::function<std::string(int value)> valueText;
   // Called when the user steps it: the new value, and the step (-1 or +1).
   std::function<void(int value, int step)> onChange;

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

private:
   float buttonWidth() const { return height() * 1.1f; }
   // -1 over the - button, +1 over the +, 0 elsewhere.
   int partAt(visage::Point p) const;
   void step(int direction);

   const Theme *theme_ = &defaultTheme();
   int min_ = -3, max_ = 3, value_ = 0, hover_ = 0;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
