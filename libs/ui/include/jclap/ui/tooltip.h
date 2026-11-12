#pragma once

// Hover help. Register widgets with their text; hovering one for a moment shows the text in a
// small box under it (wrapped onto more lines past maxWidth), and moving away or clicking hides it.
//
//    Tooltips tips;                       // a panel member
//    tips.add(cutoffKnob, "Filter cutoff. Shift-drag for fine control.");
//
// Something drawn rather than a widget, such as the plugin's name in the header, gets an invisible
// HoverArea over it to carry the tip. Every plugin shows its own version on its name this way:
//
//    tips_.add(add<ui::HoverArea>({14, 8, 200, 40}), "JSynth version " JCLAP_PLUGIN_VERSION);

#include <memory>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

// An invisible frame that is only there to be hovered for a tip. Right-clicking it offers the panel's
// background menu, as the background under it would.
class HoverArea : public visage::Frame {
public:
   void mouseDown(const visage::MouseEvent &e) override;
};

class Tooltips : public visage::EventTimer {
public:
   Tooltips();
   ~Tooltips() override;

   void add(visage::Frame &frame, std::string text);
   // Changes the text of a frame already added; it shows from the next hover. Empty text shows no tip.
   void setText(visage::Frame &frame, std::string text);
   // How long the mouse has to rest on a widget before its tip shows.
   int delayMs = 600;
   // A tip wider than this (design units) wraps onto more lines; 0 keeps every tip on one line.
   float maxWidth = 0.0f;

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
      std::vector<std::string> lines;
      float lineHeight = 0.0f;
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
