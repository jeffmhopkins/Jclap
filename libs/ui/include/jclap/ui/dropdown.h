#pragma once

// A box showing the current choice; click it for a menu of all of them, or scroll to step through.
// Fits stepped/enum params: ParamBindings maps choice i to the value firstValue + i.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Dropdown : public visage::Frame {
public:
   explicit Dropdown(std::vector<std::string> options = {}) : options_(std::move(options)) {}

   void setOptions(std::vector<std::string> options);
   const std::vector<std::string> &options() const { return options_; }

   // Opens the options in submenus instead of one list: each group's name and how many options
   // it holds, in order. Empty goes back to one list.
   void setGroups(std::vector<std::pair<std::string, int>> groups) { groups_ = std::move(groups); }

   int selected() const { return selected_; }
   // Picks the option `by` away from this one, as the user would (calls onChange).
   void step(int by);
   // Opens the options' menu, as a click on the box does.
   void openMenu();
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
   std::vector<std::pair<std::string, int>> groups_;
   int selected_ = 0;
   unsigned accent_ = 0;
   bool hover_ = false;
};

} // namespace jclap::ui
