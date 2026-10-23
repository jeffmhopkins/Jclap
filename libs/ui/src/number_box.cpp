#include "jclap/ui/number_box.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {

void NumberBox::setStepButtons(bool show) {
   stepButtons_ = show;
   redraw();
}

float NumberBox::buttonWidth() const { return stepButtons_ ? std::min(height(), width() * 0.3f) : 0.0f; }

int NumberBox::partAt(visage::Point p) const {
   const float b = buttonWidth();
   if (b <= 0.0f)
      return 0;
   if (p.x < b)
      return -1;
   if (p.x > width() - b)
      return 1;
   return 0;
}

void NumberBox::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height(), r = h * 0.18f, b = buttonWidth();
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(0, 0, w, h, r);
   if (b > 0.0f) {
      for (int side : {-1, 1}) {
         const float x = side < 0 ? 0.0f : w - b;
         canvas.setColor(hover_ == side ? t.buttonHover : t.button);
         canvas.roundedRectangle(x, 0, b, h, r);
         canvas.setColor(accent());
         const float s = h * 0.32f, cx = x + b * 0.5f, cy = h * 0.5f, line = std::max(1.5f, h * 0.06f);
         canvas.fill(cx - s * 0.5f, cy - line * 0.5f, s, line);
         if (side > 0)
            canvas.fill(cx - line * 0.5f, cy - s * 0.5f, line, s);
      }
   }
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, w, h, r, 1.0f);

   const float textW = w - 2 * b;
   if (label_.empty()) {
      canvas.setColor(t.text);
      canvas.text(text(), font(h * 0.45f), visage::Font::kCenter, b, 0, textW, h);
   }
   else {
      canvas.setColor(t.dimText);
      canvas.text(label_, font(h * 0.36f), visage::Font::kLeft, b + h * 0.25f, 0, textW, h);
      canvas.setColor(t.text);
      canvas.text(text(), font(h * 0.45f), visage::Font::kRight, b, 0, textW - h * 0.25f, h);
   }
}

void NumberBox::mouseDown(const visage::MouseEvent &e) {
   const int part = e.shouldTriggerPopup() ? 0 : partAt(e.position);
   if (part == 0) {
      ValueControl::mouseDown(e);
      return;
   }
   const double step = range().step > 0.0 ? range().step : 1.0;
   userSet(range().min + std::round((value() - range().min) / step) * step + part * step);
}

void NumberBox::mouseMove(const visage::MouseEvent &e) {
   const int part = partAt(e.position);
   if (part != hover_) {
      hover_ = part;
      redraw();
   }
}

void NumberBox::mouseExit(const visage::MouseEvent &) {
   hover_ = 0;
   redraw();
}

} // namespace jclap::ui
