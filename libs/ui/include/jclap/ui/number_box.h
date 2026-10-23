#pragma once

// A value in a box: drag up or down to change it, scroll for one step, double-click to reset, and
// right-click to type a value. With setStepButtons(true) it becomes a stepper, with - and + on
// either side that step by the range's step (1 if it has none). Fits CC numbers, channels and
// other whole-number params. Binds like a Knob.

#include "jclap/ui/value_control.h"

namespace jclap::ui {

class NumberBox : public ValueControl {
public:
   explicit NumberBox(ValueRange range = {}) : ValueControl(range) {}

   void setStepButtons(bool show);

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;

protected:
   float dragDistance(visage::Point from, visage::Point to) const override { return from.y - to.y; }
   float dragLength() const override { return 200.0f; }

private:
   // Which part of the box a point is over: -1 the - button, +1 the + button, 0 the value.
   int partAt(visage::Point p) const;
   float buttonWidth() const;

   bool stepButtons_ = false;
   int hover_ = 0;
};

} // namespace jclap::ui
