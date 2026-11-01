#pragma once

// A linear fader. Horizontal: label and value on a line above the track. Vertical: label on top,
// value at the bottom, track in between. Drag along the track to change it.

#include "jclap/ui/value_control.h"

namespace jclap::ui {

class Slider : public ValueControl {
public:
   enum class Orientation { Horizontal, Vertical };

   explicit Slider(Orientation orientation = Orientation::Horizontal, ValueRange range = {})
      : ValueControl(range), orientation_(orientation) {}

   void draw(visage::Canvas &canvas) override;
   // The track's thickness as a share of the slider's short side (0.08 by default).
   void setBarWidth(float share) {
      barShare_ = share;
      redraw();
   }

protected:
   float dragDistance(visage::Point from, visage::Point to) const override;
   float dragLength() const override;

private:
   bool vertical() const { return orientation_ == Orientation::Vertical; }
   // The track's start and length along the slider's axis, in local coordinates.
   float trackStart() const;
   float trackLength() const;

   Orientation orientation_;
   float barShare_ = 0.08f;
};

} // namespace jclap::ui
