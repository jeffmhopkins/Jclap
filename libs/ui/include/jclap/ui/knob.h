#pragma once

// A rotary knob laid out top to bottom as: label, knob, value, caption. It is drawn for a cell about
// as wide as it is tall and scales with its height. Drag up/down to change it.

#include "jclap/ui/value_control.h"

namespace jclap::ui {

class Knob : public ValueControl {
public:
   using ValueControl::ValueControl;

   void draw(visage::Canvas &canvas) override;

protected:
   float dragDistance(visage::Point from, visage::Point to) const override { return from.y - to.y; }
   // A drag of twice the knob's height covers the whole range.
   float dragLength() const override { return 2.0f * height(); }
};

} // namespace jclap::ui
