#pragma once

// A box showing the current choice; click it for a menu of all of them, or scroll to step through.
// Fits stepped/enum params: ParamBindings maps choice i to the value firstValue + i.

#include <functional>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Dropdown : public visage::Frame {
public:
   explicit Dropdown(std::vector<std::string> options = {}) : options_(std::move(options)) {}

   void setOptions(std::vector<std::string> options);
   const std::vector<std::string> &options() const { return options_; }

   int selected() const { return selected_; }
   // Changes the choice without calling onChange (for host updates).
   void setSelected(int index);

   // The param value of the first option, for ParamBindings.
   double firstValue = 0.0;

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   // Called when the user picks an option.
   std::function<void(int)> onChange;
   // Right-click menu items (e.g. the host's param menu, from ParamBindings). The menu only opens
   // if this adds something.
   std::function<void(visage::PopupMenu &)> onContextMenu;

   void draw(visage::Canvas &canvas) override;
   void mouseEnter(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

private:
   void pick(int index);

   const Theme *theme_ = &defaultTheme();
   std::vector<std::string> options_;
   int selected_ = 0;
   unsigned accent_ = 0;
   bool hover_ = false;
};

} // namespace jclap::ui
