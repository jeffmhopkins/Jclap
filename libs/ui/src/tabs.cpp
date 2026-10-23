#include "jclap/ui/tabs.h"

#include <algorithm>

namespace jclap::ui {

void TabBar::addFrame(int page, visage::Frame *frame) {
   if (page < 0 || page >= static_cast<int>(pages_.size()))
      return;
   pages_[page].push_back(frame);
   frame->setVisible(page == selected_);
}

void TabBar::select(int page) {
   if (page < 0 || page >= static_cast<int>(names_.size()) || page == selected_)
      return;
   selected_ = page;
   showSelected();
   redraw();
   if (onChange)
      onChange(page);
}

void TabBar::showSelected() {
   for (size_t p = 0; p < pages_.size(); ++p) {
      for (visage::Frame *frame : pages_[p])
         frame->setVisible(static_cast<int>(p) == selected_);
   }
}

void TabBar::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void TabBar::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

int TabBar::indexAt(float x) const {
   if (names_.empty())
      return -1;
   const int n = static_cast<int>(names_.size());
   return std::clamp(static_cast<int>(x / width() * n), 0, n - 1);
}

void TabBar::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float w = width(), h = height();
   const int n = static_cast<int>(names_.size());
   if (n == 0)
      return;
   const float tab = w / n, line = std::max(2.0f, h * 0.08f);
   canvas.setColor(t.panelBorder);
   canvas.fill(0, h - 1.0f, w, 1.0f);
   for (int i = 0; i < n; ++i) {
      const bool on = i == selected_;
      if (on || i == hover_) {
         canvas.setColor(on ? t.panel : t.buttonHover);
         canvas.roundedRectangle(i * tab, 0, tab, h + 4.0f, h * 0.18f);
      }
      if (on) {
         canvas.setColor(accent);
         canvas.fill(i * tab, h - line, tab, line);
      }
      canvas.setColor(on ? t.text : t.dimText);
      canvas.text(names_[i], font(h * 0.42f), visage::Font::kCenter, i * tab, 0, tab, h - line);
   }
}

void TabBar::mouseMove(const visage::MouseEvent &e) {
   const int i = indexAt(e.position.x);
   if (i != hover_) {
      hover_ = i;
      redraw();
   }
}

void TabBar::mouseExit(const visage::MouseEvent &) {
   hover_ = -1;
   redraw();
}

void TabBar::mouseDown(const visage::MouseEvent &e) { select(indexAt(e.position.x)); }

} // namespace jclap::ui
