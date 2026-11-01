#include "jclap/ui/slider.h"

#include <algorithm>

namespace jclap::ui {
namespace {

// Text line height and thumb size as a fraction of the slider's thickness.
constexpr float kTextLine = 0.4f;
constexpr float kThumb = 0.32f;

} // namespace

float Slider::trackStart() const {
   if (vertical())
      return height() - width() * (kTextLine + kThumb * 0.5f);
   return height() * kThumb * 0.5f;
}

float Slider::trackLength() const {
   if (vertical())
      return std::max(1.0f, height() - 2.0f * width() * (kTextLine + kThumb * 0.5f));
   return std::max(1.0f, width() - height() * kThumb);
}

float Slider::dragDistance(visage::Point from, visage::Point to) const {
   return vertical() ? from.y - to.y : to.x - from.x;
}

float Slider::dragLength() const { return trackLength(); }

void Slider::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height();
   const float thick = vertical() ? w : h; // the short side
   const float line = thick * kTextLine;
   const float bar = std::max(2.0f, thick * barShare_);
   const float thumb = thick * kThumb;

   // Positions along the axis: vertical sliders grow upwards.
   const float start = trackStart(), length = trackLength();
   const auto at = [&](double v) { return vertical() ? start - fraction(v) * length : start + fraction(v) * length; };
   const float pos = at(value()), mod = at(modulatedValue());
   const float from = at(range().bipolar ? 0.5 * (range().min + range().max) : range().min);

   // Draws a bar between two axis positions, centred across the slider at `across`.
   const auto span = [&](float a, float b, float across, float size) {
      const float lo = std::min(a, b), len = std::max(1.0f, std::abs(b - a));
      if (vertical())
         canvas.roundedRectangle(across - size * 0.5f, lo, size, len, size * 0.5f);
      else
         canvas.roundedRectangle(lo, across - size * 0.5f, len, size, size * 0.5f);
   };

   // Vertical sliders are usually wider than a horizontal one is tall, so their text runs smaller.
   const visage::Font textFont = font(line * (vertical() ? 0.5f : 0.75f));
   canvas.setColor(t.text);
   if (vertical()) {
      canvas.text(label_, textFont, visage::Font::kCenter, 0, 0, w, line);
      canvas.text(text(), textFont, visage::Font::kCenter, 0, h - line, w, line);
   }
   else {
      canvas.text(label_, textFont, visage::Font::kLeft, 0, 0, w, line);
      canvas.text(text(), textFont, visage::Font::kRight, 0, 0, w, line);
   }

   const float across = vertical() ? w * 0.5f : line + (h - line) * 0.5f;
   canvas.setColor(t.track);
   span(start, vertical() ? start - length : start + length, across, bar);
   canvas.setColor(accent());
   span(from, pos, across, bar);
   if (std::abs(mod - pos) > 0.5f) {
      canvas.setColor(t.modulation);
      span(pos, mod, across + bar * 1.5f, bar * 0.6f);
   }

   const float tx = vertical() ? across - thumb : pos - thumb * 0.5f;
   const float ty = vertical() ? pos - thumb * 0.5f : across - thumb * 0.5f;
   const float tw = vertical() ? thumb * 2.0f : thumb, th = thumb;
   canvas.setColor(t.shadow);
   canvas.roundedRectangle(tx, ty + 1.0f, tw, th, thumb * 0.25f);
   canvas.setColor(visage::Brush::linear(visage::Color(t.knobLight), visage::Color(t.knobDark),
                                         visage::Point(tx, ty), visage::Point(tx, ty + th)));
   canvas.roundedRectangle(tx, ty, tw, th, thumb * 0.25f);
   canvas.setColor(t.knobEdge);
   canvas.roundedRectangleBorder(tx, ty, tw, th, thumb * 0.25f, 1.0f);
}

} // namespace jclap::ui
