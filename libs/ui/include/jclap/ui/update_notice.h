#pragma once

// A small notice for a plugin's header that a newer JClap release is out: "Update: v0.10.3" and a
// close cross. Clicking it opens the release page; the cross hides it until the next release.
// Right-clicking offers the panel's background menu, where "Check for Updates" turns checking off.
//
// It is hidden until a check (see jclap/ui/updates.h) finds a newer release, and only ever shows
// in a window open on screen, never in a screenshot or test. Give it a spot that is free, or that
// the panel stops drawing into while the notice isVisible(): it redraws its parent when it
// appears or goes.
//
//    add<UpdateNotice>({146, 13, 190, 26});

#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"
#include "jclap/ui/updates.h"

namespace jclap::ui {

class UpdateNotice : public visage::Frame, public visage::EventTimer {
public:
   UpdateNotice();

   // Shows `release` (an empty version hides it). The timer calls this; tests and screenshots can too.
   void show(const updates::Release &release);
   const updates::Release &release() const { return release_; }

   // The pill is only as wide as its text, at the left of the bounds, or the right with this.
   bool alignRight = false;

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   void draw(visage::Canvas &canvas) override;
   void mouseEnter(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void timerCallback() override;

private:
   struct Layout {
      std::string label;
      float textSize = 0, x = 0, width = 0;
   };
   Layout layout() const;
   bool overPill(visage::Point p) const;
   bool overClose(visage::Point p) const;

   const Theme *theme_ = &defaultTheme();
   unsigned accent_ = 0;
   updates::Release release_;
   bool hover_ = false, hoverClose_ = false;
};

} // namespace jclap::ui
