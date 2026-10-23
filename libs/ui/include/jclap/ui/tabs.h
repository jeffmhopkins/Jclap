#pragma once

// Tabs along the top of a panel, each showing a page of widgets. Add a widget to a page and the
// bar shows or hides it when the page changes; widgets on no page are always shown.
//
//    auto &tabs = add<TabBar>({10, 10, 300, 28}, std::vector<std::string>{"MAIN", "ENVELOPE"});
//    tabs.addToPage(0, add<Knob>(...));
//    tabs.addToPage(1, add<EnvelopeEditor>(...));

#include <functional>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class TabBar : public visage::Frame {
public:
   explicit TabBar(std::vector<std::string> names = {}) : names_(std::move(names)), pages_(names_.size()) {}

   // Returns the frame, so it can wrap an add<>() call.
   template <typename T>
   T &addToPage(int page, T &frame) {
      addFrame(page, &frame);
      return frame;
   }

   int selected() const { return selected_; }
   // Shows that page (and calls onChange if it changed).
   void select(int page);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   std::function<void(int)> onChange;

   void draw(visage::Canvas &canvas) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;

private:
   void addFrame(int page, visage::Frame *frame);
   int indexAt(float x) const;
   void showSelected();

   const Theme *theme_ = &defaultTheme();
   std::vector<std::string> names_;
   std::vector<std::vector<visage::Frame *>> pages_;
   int selected_ = 0;
   int hover_ = -1;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
