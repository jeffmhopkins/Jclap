#include "jclap/ui/dropdown.h"

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

void Dropdown::setOptions(std::vector<std::string> options) {
   options_ = std::move(options);
   selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(options_.size()) - 1));
   redraw();
}

void Dropdown::setSelected(int index) {
   index = std::clamp(index, 0, std::max(0, static_cast<int>(options_.size()) - 1));
   if (index != selected_) {
      selected_ = index;
      redraw();
   }
}

void Dropdown::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void Dropdown::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Dropdown::pick(int index) {
   if (index < 0 || index >= static_cast<int>(options_.size()))
      return;
   selected_ = index;
   redraw();
   if (onChange)
      onChange(index);
}

void Dropdown::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float h = height(), r = h * 0.18f;
   canvas.setColor(hover_ ? t.buttonHover : t.button);
   canvas.roundedRectangle(0, 0, width(), h, r);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width(), h, r, 1.0f);

   const float pad = h * 0.3f, arrow = h * 0.22f;
   if (selected_ < static_cast<int>(options_.size())) {
      canvas.setColor(t.text);
      canvas.text(options_[selected_], font(h * 0.42f), visage::Font::kLeft, pad, 0,
                  width() - 2 * pad - arrow, h);
   }
   // Down arrow on the right.
   const float ax = width() - pad - arrow, ay = h * 0.5f - arrow * 0.3f;
   canvas.setColor(accent_ ? accent_ : t.accent);
   canvas.triangle(ax, ay, ax + arrow, ay, ax + arrow * 0.5f, ay + arrow * 0.6f);
}

void Dropdown::mouseEnter(const visage::MouseEvent &) {
   hover_ = true;
   redraw();
}

void Dropdown::mouseExit(const visage::MouseEvent &) {
   hover_ = false;
   redraw();
}

void Dropdown::mouseDown(const visage::MouseEvent &e) {
   // Right-click is the param's menu; only a plain click opens the options.
   if (e.shouldTriggerPopup()) {
      showContextMenu(this, e, onContextMenu);
      return;
   }
   if (!e.isLeftButton())
      return;
   visage::PopupMenu menu;
   if (groups_.empty())
      for (size_t i = 0; i < options_.size(); ++i)
         menu.addOption(static_cast<int>(i), options_[i]).select(static_cast<int>(i) == selected_);
   else {
      int i = 0;
      for (const auto &[name, count] : groups_) {
         visage::PopupMenu group(name);
         bool holds = false;
         for (int n = 0; n < count && i < static_cast<int>(options_.size()); ++n, ++i) {
            group.addOption(i, options_[i]).select(i == selected_);
            holds = holds || i == selected_;
         }
         menu.addSubMenu(std::move(group.select(holds)));
      }
   }
   menu.onSelection() += [this](int id) { pick(id); };
   menu.show(this);
}

bool Dropdown::mouseWheel(const visage::MouseEvent &e) {
   if (e.wheel_delta_y == 0.0f || options_.empty())
      return false;
   const int next = std::clamp(selected_ + (e.wheel_delta_y > 0 ? -1 : 1), 0, static_cast<int>(options_.size()) - 1);
   if (next != selected_)
      pick(next);
   return true;
}

} // namespace jclap::ui
