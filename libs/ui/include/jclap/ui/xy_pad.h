#pragma once

// A square pad that sets two values at once: drag the dot (x right, y up). Each axis has its own
// range, modulation and edit callbacks, so it binds to two params. Double-click resets both, and the
// values float above the pad while dragging.
//
// With setGrid(Grid::Polar) it is a circle instead: x is the angle (its range covers one full
// turn) and y the radius (center to edge).

#include <functional>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"
#include "jclap/ui/value_control.h"

namespace jclap::ui {

class XYPad : public visage::Frame {
public:
   using Axis = ValueAxis;

   enum class Grid { Cartesian, Polar };

   XYPad(ValueRange xRange = {}, ValueRange yRange = {});
   ~XYPad() override;

   Axis &x() { return axes_[0]; }
   Axis &y() { return axes_[1]; }

   // Host updates, ignored while the user drags.
   void setValues(double x, double y) { setValues(x, x, y, y); }
   void setValues(double x, double xModulated, double y, double yModulated);

   void setGrid(Grid grid);
   // Polar only: where the angle's minimum sits (degrees, 0 = right, 90 = up) and which way it grows.
   void setPolarOrigin(float degrees, bool clockwise = false);

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
   void drawCartesian(visage::Canvas &canvas, visage::Bounds b, unsigned accent);
   void drawPolar(visage::Canvas &canvas, visage::Bounds b, unsigned accent);
   // Polar: the screen angle (radians, counter-clockwise from right) for an x value, and back.
   double polarAngle(double xValue) const;
   void moveTo(visage::Point p);
   void resetBoth();
   std::string popupText() const;

   const Theme *theme_ = &defaultTheme();
   Axis axes_[2];
   unsigned accent_ = 0;
   Grid grid_ = Grid::Cartesian;
   float originDegrees_ = 0.0f;
   bool clockwise_ = false;
   bool dragging_ = false;
   ValuePopup popup_;
};

} // namespace jclap::ui
