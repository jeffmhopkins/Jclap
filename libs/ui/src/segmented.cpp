#include "jclap/ui/segmented.h"

#include <algorithm>

namespace jclap::ui {

namespace {

// Builds and opens the right-click menu from `fill`, if it adds anything.
void showContextMenu(visage::Frame *frame, const visage::MouseEvent &e,
                     const std::function<void(visage::PopupMenu &)> &fill) {
   if (!fill)
      return;
   visage::PopupMenu menu;
   fill(menu);
   if (menu.hasOptions())
      menu.show(frame, e.position);
}

} // namespace

void SegmentedSwitch::setOptions(std::vector<std::string> options) {
   options_ = std::move(options);
   selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(options_.size()) - 1));
   redraw();
}

void SegmentedSwitch::setSelected(int index) {
   index = std::clamp(index, 0, std::max(0, static_cast<int>(options_.size()) - 1));
   if (index != selected_) {
      selected_ = index;
      redraw();
   }
}

void SegmentedSwitch::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void SegmentedSwitch::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

int SegmentedSwitch::indexAt(float x) const {
   if (options_.empty())
      return -1;
   const int n = static_cast<int>(options_.size());
   return std::clamp(static_cast<int>(x / width() * n), 0, n - 1);
}

void SegmentedSwitch::pick(int index) {
   if (index < 0 || index >= static_cast<int>(options_.size()) || index == selected_)
      return;
   selected_ = index;
   redraw();
   if (onChange)
      onChange(index);
}

void SegmentedSwitch::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height(), r = h * 0.18f;
   canvas.setColor(t.button);
   canvas.roundedRectangle(0, 0, w, h, r);
   const int n = static_cast<int>(options_.size());
   if (n == 0)
      return;
   const float seg = w / n, inset = std::max(2.0f, h * 0.08f);
   for (int i = 0; i < n; ++i) {
      const float x = i * seg;
      if (i == selected_) {
         canvas.setColor(accent_ ? accent_ : t.accent);
         canvas.roundedRectangle(x + inset, inset, seg - 2 * inset, h - 2 * inset, r * 0.7f);
      }
      else if (i == hover_) {
         canvas.setColor(t.buttonHover);
         canvas.roundedRectangle(x + inset, inset, seg - 2 * inset, h - 2 * inset, r * 0.7f);
      }
      if (i > 0 && i != selected_ && i - 1 != selected_) {
         canvas.setColor(t.panelBorder);
         canvas.fill(x, h * 0.25f, 1.0f, h * 0.5f);
      }
      canvas.setColor(i == selected_ ? t.background : t.text);
      canvas.text(options_[i], font(h * 0.4f), visage::Font::kCenter, x, 0, seg, h);
   }
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, w, h, r, 1.0f);
}

void SegmentedSwitch::mouseMove(const visage::MouseEvent &e) {
   const int i = indexAt(e.position.x);
   if (i != hover_) {
      hover_ = i;
      redraw();
   }
}

void SegmentedSwitch::mouseExit(const visage::MouseEvent &) {
   hover_ = -1;
   redraw();
}

void SegmentedSwitch::mouseDown(const visage::MouseEvent &e) {
   if (e.shouldTriggerPopup()) {
      showContextMenu(this, e, onContextMenu);
      return;
   }
   if (e.isLeftButton())
      pick(indexAt(e.position.x));
}

bool SegmentedSwitch::mouseWheel(const visage::MouseEvent &e) {
   if (e.wheel_delta_y == 0.0f || options_.empty())
      return false;
   pick(std::clamp(selected_ + (e.wheel_delta_y > 0 ? -1 : 1), 0, static_cast<int>(options_.size()) - 1));
   return true;
}

} // namespace jclap::ui
