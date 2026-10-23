#pragma once

// Shared behaviour of Knob and Slider: a value in [min, max] that the user drags, scrolls or
// double-clicks back to its default, plus an optional modulated value the host is actually using.

#include <functional>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

struct ValueRange {
   double min = 0.0;
   double max = 1.0;
   double defaultValue = 0.0;
   // Mouse wheel step; 0 means 1/100 of the range.
   double step = 0.0;
   // Draw the value from the middle of the range instead of from min.
   bool bipolar = false;
};

class ValueControl : public visage::Frame {
public:
   explicit ValueControl(ValueRange range = {});

   const ValueRange &range() const { return range_; }
   void setRange(const ValueRange &range);

   double value() const { return value_; }
   double modulatedValue() const { return modulated_; }

   // Updates from the host (automation, modulation, state load). Doesn't fire the callbacks, and
   // leaves the value alone while the user is dragging.
   void setValue(double value) { setValue(value, value); }
   void setValue(double value, double modulated);

   void setLabel(std::string label);
   const std::string &label() const { return label_; }
   // Small dimmed text under the value, e.g. "CC 74".
   void setCaption(std::string caption);
   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   // How the value is shown. Defaults to a whole number when step >= 1, else two decimals.
   std::function<std::string(double)> valueText;

   // A gesture is onBeginEdit, any number of onChange, then onEndEdit.
   std::function<void()> onBeginEdit;
   std::function<void(double)> onChange;
   std::function<void()> onEndEdit;
   // Called after the user changes the value, to fetch the modulated value to show. Optional.
   std::function<double()> modulatedSource;

   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

protected:
   // Position of a value in the range, 0 to 1.
   float fraction(double value) const;
   std::string text() const;
   // How far the mouse has moved along the control's axis (up/right is positive), and how many
   // points that takes to cover the whole range.
   virtual float dragDistance(visage::Point from, visage::Point to) const = 0;
   virtual float dragLength() const = 0;
   unsigned accent() const { return accent_ ? accent_ : theme_->accent; }

   const Theme *theme_ = &defaultTheme();
   std::string label_, caption_;

private:
   void userSet(double value);

   ValueRange range_;
   double value_ = 0.0, modulated_ = 0.0;
   unsigned accent_ = 0;
   bool dragging_ = false;
   visage::Point dragStart_;
   double dragStartValue_ = 0.0;
};

} // namespace jclap::ui
