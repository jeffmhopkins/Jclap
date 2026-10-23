#pragma once

// A row of buttons where exactly one is on: every choice visible at once, for enum params with two
// to five options. Click a segment or scroll to change it. ParamBindings maps segment i to the
// value firstValue + i, like a Dropdown.

#include <functional>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class SegmentedSwitch : public visage::Frame {
public:
   explicit SegmentedSwitch(std::vector<std::string> options = {}) : options_(std::move(options)) {}

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

   void draw(visage::Canvas &canvas) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

private:
   int indexAt(float x) const;
   void pick(int index);

   const Theme *theme_ = &defaultTheme();
   std::vector<std::string> options_;
   int selected_ = 0;
   int hover_ = -1;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
