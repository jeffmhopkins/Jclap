#pragma once

// Hover help. Register widgets with their text; hovering one for a moment shows the text in a
// small box under it, and moving away or clicking hides it.
//
//    Tooltips tips;                       // a panel member
//    tips.add(cutoffKnob, "Filter cutoff. Shift-drag for fine control.");

#include <memory>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Tooltips : public visage::EventTimer {
public:
   Tooltips();
   ~Tooltips() override;

   void add(visage::Frame &frame, std::string text);
   // Changes the text of a frame already added; it shows from the next hover.
   void setText(visage::Frame &frame, std::string text);
   // How long the mouse has to rest on a widget before its tip shows.
   int delayMs = 600;

   void setTheme(const Theme &theme);
   void timerCallback() override;

   // Shows a frame's tip straight away (for screenshots and tests).
   void show(visage::Frame &frame);
   void hide();

private:
   class Popup : public visage::Frame {
   public:
      Popup() { setIgnoresMouseEvents(true, false); }
      void draw(visage::Canvas &canvas) override;
      const Theme *theme = &defaultTheme();
      std::string text;
   };

   const std::string *textFor(const visage::Frame *frame) const;

   std::vector<std::pair<visage::Frame *, std::string>> tips_;
   // What the frames' callbacks reach this through; null once this is destroyed.
   std::shared_ptr<Tooltips *> self_;
   visage::Frame *pending_ = nullptr;
   visage::Frame *host_ = nullptr;
   Popup popup_;
};

} // namespace jclap::ui
