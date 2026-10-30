#include "jclap/ui/knob.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSweep = 0.75f * kPi; // half of the 270 degree knob travel

} // namespace

void Knob::setLiveModulation(double offset, unsigned color) {
   if (offset == liveOffset_ && color == liveColor_)
      return;
   liveOffset_ = offset;
   liveColor_ = color;
   redraw();
}

void Knob::setReach(double amount, unsigned color) {
   if (amount == reach_ && color == reachColor_)
      return;
   reach_ = amount;
   reachColor_ = color;
   redraw();
}

void Knob::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height();
   const float s = h / 100.0f;
   const float d = std::min(w * 0.7f, 48.0f * s);
   const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;

   float labelSize = labelSize_ * s;
   if (fitLabel_ && !label_.empty()) {
      const float room = w - 4.0f * s;
      const float textW = font(labelSize, dpiScale()).stringWidth(visage::String(label_).toUtf32());
      if (textW > room && textW > 0.0f)
         labelSize *= std::max(0.8f, room / textW);
   }
   canvas.setColor(labelColor_ ? labelColor_ : t.text);
   canvas.text(label_, font(labelSize), visage::Font::kCenter, 0, 0, w, 16.0f * s);

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

   // Reach: drawn first and faint, so the host's and the plugin's modulation show over it.
   if (reach_ > 0.0) {
      const float lo = angleFor(std::clamp(value() - reach_, range().min, range().max));
      const float hi = angleFor(std::clamp(value() + reach_, range().min, range().max));
      if (hi - lo > 0.01f) {
         const float md = d + 7.0f * s;
         canvas.setColor((reachColor_ & 0x00ffffff) | 0x99000000);
         canvas.arc(cx - md * 0.5f, cy - md * 0.5f, md, 2.0f * s, 0.5f * (lo + hi) - 0.5f * kPi, 0.5f * (hi - lo), true);
      }
   }

   // Modulation: a thin outer arc from the knob's setting to the value in use.
   const float modAngle = angleFor(modulatedValue());
   if (std::fabs(modAngle - angle) > 0.01f) {
      const float md = d + 7.0f * s;
      canvas.setColor(t.modulation);
      canvas.arc(cx - md * 0.5f, cy - md * 0.5f, md, 2.0f * s, 0.5f * (angle + modAngle) - 0.5f * kPi,
                 0.5f * std::fabs(modAngle - angle), true);
   }
   // The plugin's own modulation carries on from there, on the same ring.
   if (liveOffset_ != 0.0) {
      const double live = std::clamp(modulatedValue() + liveOffset_, range().min, range().max);
      const float liveAngle = angleFor(live);
      if (std::fabs(liveAngle - modAngle) > 0.01f) {
         const float md = d + 7.0f * s;
         canvas.setColor(liveColor_);
         canvas.arc(cx - md * 0.5f, cy - md * 0.5f, md, 2.0f * s, 0.5f * (modAngle + liveAngle) - 0.5f * kPi,
                    0.5f * std::fabs(liveAngle - modAngle), true);
      }
   }

   // Knob body: soft shadow, shaded cap and a pointer line.
   const float body = d * 0.72f;
   canvas.setColor(t.shadow);
   canvas.circle(cx - body * 0.5f - 1.0f * s, cy - body * 0.5f + 1.5f * s, body + 2.0f * s);
   canvas.setColor(visage::Brush::radial(visage::Color(capLight_ ? capLight_ : t.knobLight),
                                         visage::Color(capDark_ ? capDark_ : t.knobDark),
                                         visage::Point(cx - body * 0.15f, cy - body * 0.2f), body * 0.65f,
                                         body * 0.65f));
   canvas.circle(cx - body * 0.5f, cy - body * 0.5f, body);
   canvas.setColor(t.knobEdge);
   canvas.ring(cx - body * 0.5f, cy - body * 0.5f, body, 1.0f * s);

   const float px = std::sin(angle), py = -std::cos(angle);
   canvas.setColor(pointer_ ? pointer_ : t.text);
   canvas.segment(cx + px * body * 0.12f, cy + py * body * 0.12f, cx + px * body * 0.4f, cy + py * body * 0.4f,
                  2.0f * s, true);

   const float textY = cy + d * 0.5f + 3.0f * s;
   canvas.setColor(t.text);
   canvas.text(text(), font(valueSize_ * s), visage::Font::kCenter, 0, textY, w, 15.0f * s);
   if (!caption_.empty()) {
      canvas.setColor(t.dimText);
      canvas.text(caption_, font(captionSize_ * s), visage::Font::kCenter, 0, textY + 14.0f * s, w, 13.0f * s);
   }
}

} // namespace jclap::ui
