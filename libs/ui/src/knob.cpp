#include "jclap/ui/knob.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSweep = 0.75f * kPi; // half of the 270 degree knob travel

} // namespace

void Knob::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height();
   const float s = h / 100.0f;
   const float d = std::min(w * 0.7f, 48.0f * s);
   const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;

   canvas.setColor(t.text);
   canvas.text(label_, font(10.5f * s), visage::Font::kCenter, 0, 0, w, 16.0f * s);

   // Track and value arc. Our angles run clockwise from 12 o'clock; Visage's start at 3 o'clock.
   const auto angleFor = [this](double v) { return -kSweep + 2.0f * kSweep * fraction(v); };
   const float angle = angleFor(value());
   const float from = range().bipolar ? 0.0f : -kSweep;
   const float ring = 3.2f * s;
   canvas.setColor(t.track);
   canvas.arc(cx - d * 0.5f, cy - d * 0.5f, d, ring, -0.5f * kPi, kSweep, true);
   if (std::fabs(angle - from) > 0.01f) {
      canvas.setColor(accent());
      canvas.arc(cx - d * 0.5f, cy - d * 0.5f, d, ring, 0.5f * (angle + from) - 0.5f * kPi,
                 0.5f * std::fabs(angle - from), true);
   }

   if (hasMarker_) {
      const float ma = angleFor(marker_), mx = std::sin(ma), my = -std::cos(ma);
      const float r0 = d * 0.5f + 1.5f * s, r1 = d * 0.5f + 6.0f * s;
      canvas.setColor(t.dimText);
      canvas.segment(cx + mx * r0, cy + my * r0, cx + mx * r1, cy + my * r1, 1.5f * s, true);
   }

   // Modulation: a thin outer arc from the knob's setting to the value in use.
   const float modAngle = angleFor(modulatedValue());
   if (std::fabs(modAngle - angle) > 0.01f) {
      const float md = d + 7.0f * s;
      canvas.setColor(t.modulation);
      canvas.arc(cx - md * 0.5f, cy - md * 0.5f, md, 2.0f * s, 0.5f * (angle + modAngle) - 0.5f * kPi,
                 0.5f * std::fabs(modAngle - angle), true);
   }

   // Knob body: soft shadow, shaded cap and a pointer line.
   const float body = d * 0.72f;
   canvas.setColor(t.shadow);
   canvas.circle(cx - body * 0.5f - 1.0f * s, cy - body * 0.5f + 1.5f * s, body + 2.0f * s);
   canvas.setColor(visage::Brush::radial(visage::Color(t.knobLight), visage::Color(t.knobDark),
                                         visage::Point(cx - body * 0.15f, cy - body * 0.2f), body * 0.65f,
                                         body * 0.65f));
   canvas.circle(cx - body * 0.5f, cy - body * 0.5f, body);
   canvas.setColor(t.knobEdge);
   canvas.ring(cx - body * 0.5f, cy - body * 0.5f, body, 1.0f * s);

   const float px = std::sin(angle), py = -std::cos(angle);
   canvas.setColor(t.text);
   canvas.segment(cx + px * body * 0.12f, cy + py * body * 0.12f, cx + px * body * 0.4f, cy + py * body * 0.4f,
                  2.0f * s, true);

   const float textY = cy + d * 0.5f + 3.0f * s;
   canvas.setColor(t.text);
   canvas.text(text(), font(12.0f * s), visage::Font::kCenter, 0, textY, w, 15.0f * s);
   if (!caption_.empty()) {
      canvas.setColor(t.dimText);
      canvas.text(caption_, font(9.5f * s), visage::Font::kCenter, 0, textY + 14.0f * s, w, 13.0f * s);
   }
}

} // namespace jclap::ui
