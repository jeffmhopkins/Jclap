#pragma once

// A rounded push button with centred text. With setToggle(true) each click flips it on or off and
// it lights up in the accent colour while on.

#include <functional>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Button : public visage::Frame {
public:
   explicit Button(std::string text = {}) : text_(std::move(text)) {}

   void setText(std::string text);
   const std::string &text() const { return text_; }

   void setToggle(bool toggle) { toggle_ = toggle; }
   bool isOn() const { return on_; }
   // Changes the toggle state without calling onToggle (for host updates).
   void setOn(bool on);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   std::function<void()> onClick;
   std::function<void(bool)> onToggle;
   // Mouse wheel over the button: +1 up, -1 down. Handy for steppers such as a channel picker.
   std::function<void(int)> onWheel;

   void draw(visage::Canvas &canvas) override;
   void mouseEnter(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

private:
   const Theme *theme_ = &defaultTheme();
   std::string text_;
   unsigned accent_ = 0;
   bool hover_ = false;
   bool toggle_ = false;
   bool on_ = false;
};

} // namespace jclap::ui
