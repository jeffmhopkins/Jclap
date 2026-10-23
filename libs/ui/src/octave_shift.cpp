#include "jclap/ui/octave_shift.h"

#include <algorithm>

namespace jclap::ui {

OctaveShift::OctaveShift(int min, int max) { setRange(min, max); }

void OctaveShift::setRange(int min, int max) {
   min_ = std::min(min, max);
   max_ = std::max(min, max);
   value_ = std::clamp(value_, min_, max_);
   redraw();
}

void OctaveShift::setValue(int value) {
   value = std::clamp(value, min_, max_);
   if (value != value_) {
      value_ = value;
      redraw();
   }
}

void OctaveShift::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void OctaveShift::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

int OctaveShift::partAt(visage::Point p) const {
   if (p.x < buttonWidth())
      return -1;
   if (p.x >= width() - buttonWidth())
      return 1;
   return 0;
}

void OctaveShift::step(int direction) {
   const int next = std::clamp(value_ + direction, min_, max_);
   if (next == value_)
      return;
   value_ = next;
   redraw();
   if (onChange)
      onChange(value_, direction);
}

void OctaveShift::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float w = width(), h = height(), r = h * 0.18f, b = buttonWidth();
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(0, 0, w, h, r);
   for (int side : {-1, 1}) {
      const float x = side < 0 ? 0.0f : w - b;
      const bool atEnd = side < 0 ? value_ <= min_ : value_ >= max_;
      canvas.setColor(hover_ == side && !atEnd ? t.buttonHover : t.button);
      canvas.roundedRectangle(x, 0, b, h, r);
      canvas.setColor(atEnd ? t.track : accent);
      const float s = h * 0.32f, cx = x + b * 0.5f, cy = h * 0.5f, line = std::max(1.5f, h * 0.06f);
      canvas.fill(cx - s * 0.5f, cy - line * 0.5f, s, line);
      if (side > 0)
         canvas.fill(cx - line * 0.5f, cy - s * 0.5f, line, s);
   }
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, w, h, r, 1.0f);
   std::string text;
   if (valueText)
      text = valueText(value_);
   else
      text = "OCT " + std::string(value_ > 0 ? "+" : "") + std::to_string(value_);
   canvas.setColor(t.text);
   canvas.text(text, font(h * 0.45f), visage::Font::kCenter, b, 0, std::max(0.0f, w - 2 * b), h);
}

void OctaveShift::mouseDown(const visage::MouseEvent &e) { step(partAt(e.position)); }

void OctaveShift::mouseMove(const visage::MouseEvent &e) {
   const int part = partAt(e.position);
   if (part != hover_) {
      hover_ = part;
      redraw();
   }
}

void OctaveShift::mouseExit(const visage::MouseEvent &) {
   hover_ = 0;
   redraw();
}

bool OctaveShift::mouseWheel(const visage::MouseEvent &e) {
   if (e.wheel_delta_y == 0.0f)
      return false;
   step(e.wheel_delta_y > 0.0f ? 1 : -1);
   return true;
}

} // namespace jclap::ui
