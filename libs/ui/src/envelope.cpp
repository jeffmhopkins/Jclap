#include "jclap/ui/envelope.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {
namespace {

// Each of attack, decay and release gets this share of the width at its maximum; the sustain
// plateau always gets the rest.
constexpr float kStageWidth = 0.3f;

} // namespace

EnvelopeEditor::EnvelopeEditor(ValueRange attack, ValueRange decay, ValueRange sustain, ValueRange release) {
   const ValueRange ranges[kStageCount] = {attack, decay, sustain, release};
   const char *labels[kStageCount] = {"A", "D", "S", "R"};
   for (int i = 0; i < kStageCount; ++i) {
      stages_[i].range = ranges[i];
      stages_[i].label = labels[i];
      stages_[i].value = stages_[i].modulated = ranges[i].clamp(ranges[i].defaultValue);
   }
}

EnvelopeEditor::~EnvelopeEditor() {
   // Destroyed mid-drag: close the gestures mouseUp would have.
   EnvelopeEditor::mouseUp({});
   popup_.hide();
}

void EnvelopeEditor::setValues(double attack, double decay, double sustain, double release) {
   const double v[kStageCount] = {attack, decay, sustain, release};
   for (int i = 0; i < kStageCount; ++i)
      setValue(static_cast<Stage>(i), v[i], v[i]);
}

void EnvelopeEditor::setValue(Stage s, double value, double modulated) {
   ValueAxis &a = stages_[s];
   if (dragging_ != kNone)
      value = a.value;
   if (value != a.value || modulated != a.modulated) {
      a.value = value;
      a.modulated = modulated;
      redraw();
   }
}

void EnvelopeEditor::setPlayheads(std::vector<Playhead> playheads) {
   if (playheads == playheads_)
      return;
   playheads_ = std::move(playheads);
   redraw();
}

void EnvelopeEditor::setTimeCurve(float curve) {
   timeCurve_ = std::max(0.05f, curve);
   redraw();
}

void EnvelopeEditor::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void EnvelopeEditor::setTheme(const Theme &theme) {
   theme_ = &theme;
   popup_.setTheme(theme);
   redraw();
}

visage::Bounds EnvelopeEditor::area() const {
   const float pad = std::max(6.0f, std::min(width(), height()) * 0.06f);
   return {pad, pad, std::max(1.0f, width() - 2 * pad), std::max(1.0f, height() - 2 * pad)};
}

EnvelopeEditor::Points EnvelopeEditor::points(bool modulated) const {
   const visage::Bounds b = area();
   const auto f = [&](Stage s) {
      const ValueAxis &a = stages_[s];
      const float fraction = std::clamp(a.range.fraction(modulated ? a.modulated : a.value), 0.0f, 1.0f);
      return s == kSustain ? fraction : std::pow(fraction, timeCurve_);
   };
   const float stage = b.width() * kStageWidth;
   Points p;
   p.start = {b.x(), b.bottom()};
   p.peak = {b.x() + f(kAttack) * stage, b.y()};
   p.knee = {p.peak.x + f(kDecay) * stage, b.bottom() - f(kSustain) * b.height()};
   p.hold = {p.knee.x + b.width() * (1.0f - 3.0f * kStageWidth), p.knee.y};
   p.end = {p.hold.x + f(kRelease) * stage, b.bottom()};
   return p;
}

void EnvelopeEditor::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float r = std::min(width(), height()) * 0.04f;
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(0, 0, width(), height(), r);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width(), height(), r, 1.0f);

   const visage::Bounds b = area();
   canvas.setColor(t.track);
   for (int i = 1; i < 4; ++i)
      canvas.fill(b.x(), b.y() + b.height() * i / 4.0f, b.width(), 1.0f);

   const Points p = points(false);
   const visage::Point line[] = {p.start, p.peak, p.knee, p.hold, p.end};

   // Fill under the curve, one column at a time.
   canvas.setColor((accent & 0x00ffffff) | 0x30000000);
   for (int i = 0; i + 1 < 5; ++i) {
      const visage::Point a = line[i], c = line[i + 1];
      const float span = c.x - a.x;
      for (float x = a.x; x < c.x; x += 1.0f) {
         const float y = span > 0.0f ? a.y + (c.y - a.y) * (x - a.x) / span : a.y;
         canvas.fill(x, y, std::min(1.0f, c.x - x), b.bottom() - y);
      }
   }

   // The modulated shape, if the host is moving any stage.
   const Points m = points(true);
   const visage::Point modLine[] = {m.start, m.peak, m.knee, m.hold, m.end};
   bool modulated = false;
   for (const ValueAxis &a : stages_)
      modulated = modulated || a.modulated != a.value;
   if (modulated) {
      // Drawn like a knob's modulation ring: a light line, with a small dot on each moved corner.
      canvas.setColor(t.modulation);
      const float line = std::max(1.5f, std::min(width(), height()) * 0.012f);
      for (int i = 0; i + 1 < 5; ++i)
         canvas.segment(modLine[i].x, modLine[i].y, modLine[i + 1].x, modLine[i + 1].y, line, true);
      const float pip = line * 3.0f;
      for (int i = 1; i < 5; ++i)
         canvas.circle(modLine[i].x - pip * 0.5f, modLine[i].y - pip * 0.5f, pip);
   }

   canvas.setColor(accent);
   for (int i = 0; i + 1 < 5; ++i)
      canvas.segment(line[i].x, line[i].y, line[i + 1].x, line[i + 1].y, 2.0f, true);

   const float dot = std::max(8.0f, std::min(width(), height()) * 0.07f);
   for (const visage::Point &h : {p.peak, p.knee, p.end}) {
      canvas.setColor(t.knobDark);
      canvas.circle(h.x - dot * 0.5f, h.y - dot * 0.5f, dot);
      canvas.setColor(accent);
      canvas.ring(h.x - dot * 0.5f, h.y - dot * 0.5f, dot, 2.0f);
   }

   // Each playhead rides the curve the host is playing (the modulated one): along each stage by how
   // far the level has travelled through it, at the level's height.
   const float sustain = std::clamp(stages_[kSustain].range.fraction(stages_[kSustain].modulated), 0.0f, 1.0f);
   const auto along = [](visage::Point a, visage::Point c, float f) {
      f = std::clamp(f, 0.0f, 1.0f);
      return visage::Point{a.x + (c.x - a.x) * f, a.y + (c.y - a.y) * f};
   };
   const float glow = dot * 2.2f;
   for (const Playhead &ph : playheads_) {
      const float level = std::clamp(ph.level, 0.0f, 1.0f);
      visage::Point at;
      if (ph.stage == kAttack)
         at = along(m.start, m.peak, level);
      else if (ph.stage == kDecay)
         at = along(m.peak, m.knee, sustain < 1.0f ? (1.0f - level) / (1.0f - sustain) : 1.0f);
      else if (ph.stage == kSustain)
         at = m.knee;
      else
         at = along(m.hold, m.end, ph.releaseFrom > 0.0f ? 1.0f - level / ph.releaseFrom : 1.0f);
      at.y = b.bottom() - level * b.height();
      canvas.setColor((accent & 0x00ffffff) | 0x30000000);
      canvas.circle(at.x - glow * 0.5f, at.y - glow * 0.5f, glow);
      canvas.setColor((accent & 0x00ffffff) | 0x60000000);
      canvas.circle(at.x - glow * 0.32f, at.y - glow * 0.32f, glow * 0.64f);
      canvas.setColor(t.modulation);
      canvas.circle(at.x - dot * 0.3f, at.y - dot * 0.3f, dot * 0.6f);
   }
}

EnvelopeEditor::Handle EnvelopeEditor::handleAt(visage::Point p) const {
   const Points pts = points(false);
   const visage::Point handles[] = {pts.peak, pts.knee, pts.end};
   Handle best = kNone;
   float bestDistance = std::max(14.0f, std::min(width(), height()) * 0.12f);
   for (int i = 0; i < 3; ++i) {
      const float d = std::hypot(p.x - handles[i].x, p.y - handles[i].y);
      if (d < bestDistance) {
         bestDistance = d;
         best = static_cast<Handle>(i);
      }
   }
   return best;
}

void EnvelopeEditor::stagesOf(Handle h, Stage &a, Stage &b) const {
   a = h == kPeak ? kAttack : h == kKnee ? kDecay : kRelease;
   b = h == kKnee ? kSustain : a;
}

void EnvelopeEditor::moveTo(visage::Point p) {
   const visage::Bounds b = area();
   const float stage = b.width() * kStageWidth;
   const auto fromFraction = [](ValueAxis &a, float f) {
      a.set(a.range.min + std::clamp(f, 0.0f, 1.0f) * (a.range.max - a.range.min));
   };
   // Times: undo the drawing curve.
   const auto fromPosition = [&](ValueAxis &a, float f) {
      fromFraction(a, std::pow(std::clamp(f, 0.0f, 1.0f), 1.0f / timeCurve_));
   };
   const Points pts = points(false);
   switch (dragging_) {
   case kPeak:
      fromPosition(stages_[kAttack], (p.x - b.x()) / stage);
      break;
   case kKnee:
      fromPosition(stages_[kDecay], (p.x - pts.peak.x) / stage);
      fromFraction(stages_[kSustain], (b.bottom() - p.y) / b.height());
      break;
   case kEnd:
      fromPosition(stages_[kRelease], (p.x - pts.hold.x) / stage);
      break;
   default:
      break;
   }
   redraw();
}

void EnvelopeEditor::resetHandle(Handle h) {
   Stage a, b;
   stagesOf(h, a, b);
   for (Stage s : {a, b}) {
      ValueAxis &axis = stages_[s];
      if (axis.onBeginEdit)
         axis.onBeginEdit();
      axis.set(axis.range.defaultValue);
      if (axis.onEndEdit)
         axis.onEndEdit();
      if (a == b)
         break;
   }
   redraw();
}

std::string EnvelopeEditor::popupText() const {
   Stage a, b;
   stagesOf(dragging_, a, b);
   std::string text = stages_[a].label + " " + stages_[a].text(stages_[a].modulated);
   if (b != a)
      text += "  " + stages_[b].label + " " + stages_[b].text(stages_[b].modulated);
   return text;
}

void EnvelopeEditor::mouseDown(const visage::MouseEvent &e) {
   const Handle h = handleAt(e.position);
   if (e.shouldTriggerPopup()) {
      visage::PopupMenu menu;
      menu.addOption(0, "Reset to default").onSelection() += [this](int) {
         for (Handle each : {kPeak, kKnee, kEnd})
            resetHandle(each);
      };
      if (onContextMenu)
         onContextMenu(menu);
      menu.show(this, e.position);
      return;
   }
   if (h == kNone)
      return;
   if (e.repeatClickCount() == 2) {
      dragging_ = kNone;
      resetHandle(h);
      return;
   }
   dragging_ = h;
   Stage a, b;
   stagesOf(h, a, b);
   if (stages_[a].onBeginEdit)
      stages_[a].onBeginEdit();
   if (b != a && stages_[b].onBeginEdit)
      stages_[b].onBeginEdit();
   popup_.show(*this, popupText());
}

void EnvelopeEditor::mouseDrag(const visage::MouseEvent &e) {
   if (dragging_ == kNone)
      return;
   moveTo(e.position);
   popup_.show(*this, popupText());
}

void EnvelopeEditor::mouseUp(const visage::MouseEvent &) {
   if (dragging_ != kNone) {
      Stage a, b;
      stagesOf(dragging_, a, b);
      if (stages_[a].onEndEdit)
         stages_[a].onEndEdit();
      if (b != a && stages_[b].onEndEdit)
         stages_[b].onEndEdit();
   }
   dragging_ = kNone;
   popup_.hide();
}

} // namespace jclap::ui
