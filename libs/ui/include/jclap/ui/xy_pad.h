#pragma once

// A square pad that sets two values at once: drag the dot (x right, y up). Each axis has its own
// range, modulation and edit callbacks, so it binds to two params. Double-click resets both, and the
// values float above the pad while dragging.

#include <functional>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"
#include "jclap/ui/value_control.h"

namespace jclap::ui {

class XYPad : public visage::Frame {
public:
   struct Axis {
      ValueRange range;
      std::string label;
      double value = 0.0, modulated = 0.0;
      std::function<std::string(double)> valueText;
      std::function<void()> onBeginEdit;
      std::function<void(double)> onChange;
      std::function<void()> onEndEdit;
      std::function<double()> modulatedSource;
   };

   XYPad(ValueRange xRange = {}, ValueRange yRange = {});
   ~XYPad() override;

   Axis &x() { return axes_[0]; }
   Axis &y() { return axes_[1]; }

   // Host updates, ignored while the user drags.
   void setValues(double x, double y) { setValues(x, x, y, y); }
   void setValues(double x, double xModulated, double y, double yModulated);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   // Adds items to the right-click menu, after "Reset to default".
   std::function<void(visage::PopupMenu &)> onContextMenu;

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;

private:
   visage::Bounds pad() const;
   void moveTo(visage::Point p);
   void set(Axis &a, double v);
   void resetBoth();
   std::string popupText() const;

   const Theme *theme_ = &defaultTheme();
   Axis axes_[2];
   unsigned accent_ = 0;
   bool dragging_ = false;
   ValuePopup popup_;
};

} // namespace jclap::ui
