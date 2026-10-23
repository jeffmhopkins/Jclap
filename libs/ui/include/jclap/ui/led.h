#pragma once

// A small light that flashes on activity, such as MIDI coming in or going out, then fades. Call
// flash() from the GUI, or feed it a counter the audio thread bumps on each event with
// setCount(): it flashes whenever the count changes, so no event is missed between redraws.

#include <cstdint>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class ActivityLed : public visage::Frame, public visage::EventTimer {
public:
   explicit ActivityLed(std::string label = {});

   void flash();
   void setCount(uint32_t count);
   // Brightness 0..1, normally left to flash() and the fade. For screenshots and tests.
   void setLevel(float level);

   void setLabel(std::string label);
   void setColor(unsigned color);
   void setTheme(const Theme &theme);
   // How long a flash takes to fade out.
   int fadeMs = 250;

   void draw(visage::Canvas &canvas) override;
   void timerCallback() override;

private:
   const Theme *theme_ = &defaultTheme();
   std::string label_;
   unsigned color_ = 0;
   float level_ = 0.0f;
   uint32_t count_ = 0;
   bool counted_ = false;
};

} // namespace jclap::ui
