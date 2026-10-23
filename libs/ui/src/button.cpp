#include "jclap/ui/button.h"

namespace jclap::ui {

void Button::setText(std::string text) {
   if (text != text_) {
      text_ = std::move(text);
      redraw();
   }
}

void Button::setOn(bool on) {
   if (on != on_) {
      on_ = on;
      redraw();
   }
}

void Button::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void Button::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Button::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float r = height() * 0.18f;
   canvas.setColor(on_ ? accent : hover_ ? t.buttonHover : t.button);
   canvas.roundedRectangle(0, 0, width(), height(), r);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width(), height(), r, 1.0f);
   canvas.setColor(on_ ? t.background : t.text);
   canvas.text(text_, font(height() * 0.42f), visage::Font::kCenter, 0, 0, width(), height());
}

void Button::mouseEnter(const visage::MouseEvent &) {
   hover_ = true;
   redraw();
}

void Button::mouseExit(const visage::MouseEvent &) {
   hover_ = false;
   redraw();
}

void Button::mouseDown(const visage::MouseEvent &) {
   if (toggle_) {
      setOn(!on_);
      if (onToggle)
         onToggle(on_);
   }
   if (onClick)
      onClick();
}

bool Button::mouseWheel(const visage::MouseEvent &e) {
   if (!onWheel || e.wheel_delta_y == 0.0f)
      return false;
   onWheel(e.wheel_delta_y > 0 ? 1 : -1);
   return true;
}

} // namespace jclap::ui
