#include "jclap/ui/value_control.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace jclap::ui {

ValueControl::ValueControl(ValueRange range) : range_(range) {
   value_ = modulated_ = std::clamp(range_.defaultValue, range_.min, range_.max);
}

void ValueControl::setRange(const ValueRange &range) {
   range_ = range;
   value_ = std::clamp(value_, range_.min, range_.max);
   modulated_ = std::clamp(modulated_, range_.min, range_.max);
   redraw();
}

void ValueControl::setValue(double value, double modulated) {
   if (dragging_)
      value = value_;
   if (value != value_ || modulated != modulated_) {
      value_ = value;
      modulated_ = modulated;
      redraw();
   }
}

void ValueControl::setLabel(std::string label) {
   label_ = std::move(label);
   redraw();
}

void ValueControl::setCaption(std::string caption) {
   caption_ = std::move(caption);
   redraw();
}

void ValueControl::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void ValueControl::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

float ValueControl::fraction(double value) const {
   const double span = range_.max - range_.min;
   return span > 0.0 ? static_cast<float>((value - range_.min) / span) : 0.0f;
}

std::string ValueControl::text() const {
   if (valueText)
      return valueText(modulated_);
   if (range_.step >= 1.0)
      return std::to_string(std::lround(modulated_));
   char buffer[32];
   std::snprintf(buffer, sizeof(buffer), "%.2f", modulated_);
   return buffer;
}

void ValueControl::userSet(double value) {
   value_ = std::clamp(value, range_.min, range_.max);
   if (onChange)
      onChange(value_);
   modulated_ = modulatedSource ? modulatedSource() : value_;
   redraw();
}

void ValueControl::mouseDown(const visage::MouseEvent &e) {
   if (e.repeatClickCount() == 2) {
      if (onBeginEdit)
         onBeginEdit();
      userSet(range_.defaultValue);
      if (onEndEdit)
         onEndEdit();
      dragging_ = false;
      return;
   }
   dragging_ = true;
   dragStart_ = e.position;
   dragStartValue_ = value_;
   if (onBeginEdit)
      onBeginEdit();
}

void ValueControl::mouseDrag(const visage::MouseEvent &e) {
   if (!dragging_)
      return;
   // Hold shift for fine control.
   const float length = dragLength() * (e.isShiftDown() ? 10.0f : 1.0f);
   userSet(dragStartValue_ + dragDistance(dragStart_, e.position) * (range_.max - range_.min) / length);
}

void ValueControl::mouseUp(const visage::MouseEvent &) {
   if (dragging_ && onEndEdit)
      onEndEdit();
   dragging_ = false;
}

bool ValueControl::mouseWheel(const visage::MouseEvent &e) {
   const float delta = e.precise_wheel_delta_y != 0.0f ? e.precise_wheel_delta_y : e.wheel_delta_y;
   if (delta == 0.0f)
      return false;
   const double step = range_.step > 0.0 ? range_.step : (range_.max - range_.min) / 100.0;
   if (onBeginEdit)
      onBeginEdit();
   userSet(range_.min + std::round((value_ - range_.min) / step) * step + (delta > 0 ? step : -step));
   if (onEndEdit)
      onEndEdit();
   return true;
}

} // namespace jclap::ui
