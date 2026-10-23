#include "jclap/ui/section.h"

#include <algorithm>

namespace jclap::ui {

Section::Section(std::string title) : title_(std::move(title)) { setIgnoresMouseEvents(true, false); }

void Section::setTitle(std::string title) {
   title_ = std::move(title);
   redraw();
}

void Section::setScale(float scale) {
   scale_ = scale;
   redraw();
}

void Section::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Section::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float s = scale_;
   canvas.setColor(t.panel);
   canvas.roundedRectangle(0, 0, width(), height(), 6 * s);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width(), height(), 6 * s, 1.0f);
   if (!title_.empty()) {
      canvas.setColor(t.accent);
      canvas.text(title_, font(15 * s), visage::Font::kLeft, 14 * s, 6 * s, width(), 22 * s);
   }
}

Label::Label(std::string text, unsigned color, visage::Font::Justification justification, float fontScale)
   : text_(std::move(text)), color_(color), justification_(justification), fontScale_(fontScale) {
   setIgnoresMouseEvents(true, false);
}

void Label::setText(std::string text) {
   if (text != text_) {
      text_ = std::move(text);
      redraw();
   }
}

void Label::setColor(unsigned color) {
   color_ = color;
   redraw();
}

void Label::setFontScale(float scale) {
   fontScale_ = scale;
   redraw();
}

void Label::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Label::draw(visage::Canvas &canvas) {
   canvas.setColor(color_ ? color_ : theme_->text);
   canvas.text(text_, font(fontScale_ * height()), justification_, 0, 0, width(), height());
}

Divider::Divider() { setIgnoresMouseEvents(true, false); }

void Divider::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Divider::draw(visage::Canvas &canvas) {
   canvas.setColor(theme_->panelBorder);
   canvas.fill(0, 0, std::max(1.0f, width()), std::max(1.0f, height()));
}

} // namespace jclap::ui
