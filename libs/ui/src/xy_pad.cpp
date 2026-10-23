#include "jclap/ui/xy_pad.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {
namespace {

constexpr double kTwoPi = 6.283185307179586;

} // namespace

XYPad::XYPad(ValueRange xRange, ValueRange yRange) {
   axes_[0].range = xRange;
   axes_[1].range = yRange;
   for (Axis &a : axes_)
      a.value = a.modulated = a.range.clamp(a.range.defaultValue);
}

XYPad::~XYPad() { popup_.hide(); }

void XYPad::setValues(double x, double xModulated, double y, double yModulated) {
   const double v[2][2] = {{x, xModulated}, {y, yModulated}};
   bool changed = false;
   for (int i = 0; i < 2; ++i) {
      Axis &a = axes_[i];
      const double value = dragging_ ? a.value : v[i][0];
      if (value != a.value || v[i][1] != a.modulated) {
         a.value = value;
         a.modulated = v[i][1];
         changed = true;
      }
   }
   if (changed)
      redraw();
}

void XYPad::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void XYPad::setTheme(const Theme &theme) {
   theme_ = &theme;
   popup_.setTheme(theme);
   redraw();
}

void XYPad::setGrid(Grid grid) {
   grid_ = grid;
   redraw();
}

void XYPad::setPolarOrigin(float degrees, bool clockwise) {
   originDegrees_ = degrees;
   clockwise_ = clockwise;
   redraw();
}

double XYPad::polarAngle(double xValue) const {
   const double turn = axes_[0].range.fraction(xValue) * kTwoPi;
   return originDegrees_ * kTwoPi / 360.0 + (clockwise_ ? -turn : turn);
}

visage::Bounds XYPad::pad() const {
   const float label = std::min(width(), height()) * 0.1f;
   return {0.0f, 0.0f, width(), height() - label};
}

void XYPad::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const visage::Bounds b = pad();
   const unsigned accent = accent_ ? accent_ : t.accent;
   if (grid_ == Grid::Polar)
      drawPolar(canvas, b, accent);
   else
      drawCartesian(canvas, b, accent);

   const float label = height() - b.height();
   canvas.setColor(t.dimText);
   canvas.text(axes_[0].label, font(label * 0.75f), visage::Font::kLeft, 0, b.bottom(), width() * 0.5f, label);
   canvas.text(axes_[1].label, font(label * 0.75f), visage::Font::kRight, width() * 0.5f, b.bottom(),
               width() * 0.5f, label);
}

void XYPad::drawCartesian(visage::Canvas &canvas, visage::Bounds b, unsigned accent) {
   const Theme &t = *theme_;
   const float r = std::min(b.width(), b.height()) * 0.04f;
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(b.x(), b.y(), b.width(), b.height(), r);
   canvas.setColor(t.track);
   for (int i = 1; i < 4; ++i) {
      canvas.fill(b.x() + b.width() * i / 4.0f, b.y(), 1.0f, b.height());
      canvas.fill(b.x(), b.y() + b.height() * i / 4.0f, b.width(), 1.0f);
   }
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), r, 1.0f);

   const auto at = [&](double xv, double yv) {
      return visage::Point(b.x() + axes_[0].range.fraction(xv) * b.width(),
                           b.bottom() - axes_[1].range.fraction(yv) * b.height());
   };
   const visage::Point p = at(axes_[0].value, axes_[1].value);
   const visage::Point m = at(axes_[0].modulated, axes_[1].modulated);
   const float dot = std::min(b.width(), b.height()) * 0.07f;

   // Crosshair through the dot.
   canvas.setColor((accent & 0x00ffffff) | 0x55000000);
   canvas.fill(p.x, b.y(), 1.0f, b.height());
   canvas.fill(b.x(), p.y, b.width(), 1.0f);

   if (std::abs(m.x - p.x) > 0.5f || std::abs(m.y - p.y) > 0.5f) {
      canvas.setColor(t.modulation);
      canvas.segment(p.x, p.y, m.x, m.y, 1.5f, true);
      canvas.ring(m.x - dot * 0.4f, m.y - dot * 0.4f, dot * 0.8f, 1.5f);
   }
   canvas.setColor(accent);
   canvas.circle(p.x - dot * 0.5f, p.y - dot * 0.5f, dot);
}

void XYPad::drawPolar(visage::Canvas &canvas, visage::Bounds b, unsigned accent) {
   const Theme &t = *theme_;
   const float size = std::min(b.width(), b.height());
   const float dot = size * 0.07f;
   const float radius = size * 0.5f - 1.0f;
   const float cx = b.x() + b.width() * 0.5f, cy = b.y() + b.height() * 0.5f;
   const auto at = [&](double angle, double r) {
      return visage::Point(cx + static_cast<float>(std::cos(angle) * r), cy - static_cast<float>(std::sin(angle) * r));
   };

   canvas.setColor(t.knobDark);
   canvas.circle(cx - radius, cy - radius, 2 * radius);
   canvas.setColor(t.track);
   for (int i = 1; i < 4; ++i) {
      const float ring = radius * i / 4.0f;
      canvas.ring(cx - ring, cy - ring, 2 * ring, 1.0f);
   }
   for (int i = 0; i < 8; ++i) {
      const visage::Point e = at(i * kTwoPi / 8.0, radius);
      canvas.segment(cx, cy, e.x, e.y, 1.0f, false);
   }
   canvas.setColor(t.knobLight);
   canvas.ring(cx - radius, cy - radius, 2 * radius, 1.0f);

   // Where the angle starts.
   const double origin = polarAngle(axes_[0].range.min);
   const visage::Point o1 = at(origin, radius * 0.86f), o2 = at(origin, radius);
   canvas.setColor(t.dimText);
   canvas.segment(o1.x, o1.y, o2.x, o2.y, 2.0f, true);

   const double angle = polarAngle(axes_[0].value);
   const double r = axes_[1].range.fraction(axes_[1].value) * (radius - dot * 0.5f);
   const visage::Point p = at(angle, r);
   const visage::Point m = at(polarAngle(axes_[0].modulated),
                              axes_[1].range.fraction(axes_[1].modulated) * (radius - dot * 0.5f));

   // The radius as a line from the center, and the angle as an arc around it.
   canvas.setColor((accent & 0x00ffffff) | 0x55000000);
   canvas.ring(cx - r, cy - r, 2 * r, 1.0f);
   canvas.setColor(accent);
   canvas.segment(cx, cy, p.x, p.y, 2.0f, true);

   if (std::abs(m.x - p.x) > 0.5f || std::abs(m.y - p.y) > 0.5f) {
      canvas.setColor(t.modulation);
      canvas.segment(p.x, p.y, m.x, m.y, 1.5f, true);
      canvas.ring(m.x - dot * 0.4f, m.y - dot * 0.4f, dot * 0.8f, 1.5f);
   }
   canvas.setColor(accent);
   canvas.circle(p.x - dot * 0.5f, p.y - dot * 0.5f, dot);
}

std::string XYPad::popupText() const {
   return axes_[0].text(axes_[0].modulated) + "  " + axes_[1].text(axes_[1].modulated);
}

void XYPad::moveTo(visage::Point p) {
   const visage::Bounds b = pad();
   double fx, fy;
   if (grid_ == Grid::Polar) {
      const float radius = std::max(1.0f, std::min(b.width(), b.height()) * 0.5f);
      const double dx = p.x - (b.x() + b.width() * 0.5f), dy = (b.y() + b.height() * 0.5f) - p.y;
      double turn = std::atan2(dy, dx) - originDegrees_ * kTwoPi / 360.0;
      if (clockwise_)
         turn = -turn;
      turn = std::fmod(turn, kTwoPi);
      fx = (turn < 0.0 ? turn + kTwoPi : turn) / kTwoPi;
      fy = std::clamp(std::hypot(dx, dy) / radius, 0.0, 1.0);
   }
   else {
      fx = std::clamp((p.x - b.x()) / std::max(1.0f, b.width()), 0.0f, 1.0f);
      fy = std::clamp((b.bottom() - p.y) / std::max(1.0f, b.height()), 0.0f, 1.0f);
   }
   axes_[0].set(axes_[0].range.min + fx * (axes_[0].range.max - axes_[0].range.min));
   axes_[1].set(axes_[1].range.min + fy * (axes_[1].range.max - axes_[1].range.min));
   redraw();
}

void XYPad::resetBoth() {
   for (Axis &a : axes_) {
      if (a.onBeginEdit)
         a.onBeginEdit();
      a.set(a.range.defaultValue);
      if (a.onEndEdit)
         a.onEndEdit();
   }
   redraw();
}

void XYPad::mouseDown(const visage::MouseEvent &e) {
   if (e.shouldTriggerPopup()) {
      visage::PopupMenu menu;
      menu.addOption(0, "Reset to default").onSelection() += [this](int) { resetBoth(); };
      if (onContextMenu)
         onContextMenu(menu);
      menu.show(this, e.position);
      return;
   }
   if (e.repeatClickCount() == 2) {
      dragging_ = false;
      resetBoth();
      return;
   }
   dragging_ = true;
   for (Axis &a : axes_) {
      if (a.onBeginEdit)
         a.onBeginEdit();
   }
   moveTo(e.position);
   popup_.show(*this, popupText());
}

void XYPad::mouseDrag(const visage::MouseEvent &e) {
   if (!dragging_)
      return;
   moveTo(e.position);
   popup_.show(*this, popupText());
}

void XYPad::mouseUp(const visage::MouseEvent &) {
   if (dragging_) {
      for (Axis &a : axes_) {
         if (a.onEndEdit)
            a.onEndEdit();
      }
   }
   dragging_ = false;
   popup_.hide();
}

} // namespace jclap::ui
