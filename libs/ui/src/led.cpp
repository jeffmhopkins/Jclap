#include "jclap/ui/led.h"

#include <algorithm>

namespace jclap::ui {
namespace {

constexpr int kTickMs = 30;

unsigned blend(unsigned from, unsigned to, float amount) {
   unsigned out = 0xff000000;
   for (int shift = 0; shift < 24; shift += 8) {
      const float a = static_cast<float>((from >> shift) & 0xff), b = static_cast<float>((to >> shift) & 0xff);
      out |= static_cast<unsigned>(a + (b - a) * amount) << shift;
   }
   return out;
}

} // namespace

ActivityLed::ActivityLed(std::string label) : label_(std::move(label)) { setIgnoresMouseEvents(true, false); }

void ActivityLed::flash() { setLevel(1.0f); }

void ActivityLed::setCount(uint32_t count) {
   if (counted_ && count != count_)
      flash();
   count_ = count;
   counted_ = true;
}

void ActivityLed::setLevel(float level) {
   level = std::clamp(level, 0.0f, 1.0f);
   if (level == level_)
      return;
   level_ = level;
   if (level_ > 0.0f && fadeMs > 0 && !isRunning())
      startTimer(kTickMs);
   redraw();
}

void ActivityLed::setLabel(std::string label) {
   label_ = std::move(label);
   redraw();
}

void ActivityLed::setColor(unsigned color) {
   color_ = color;
   redraw();
}

void ActivityLed::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void ActivityLed::timerCallback() {
   if (fadeMs <= 0) {
      stopTimer();
      return;
   }
   level_ = std::max(0.0f, level_ - static_cast<float>(kTickMs) / std::max(1, fadeMs));
   if (level_ <= 0.0f)
      stopTimer();
   redraw();
}

void ActivityLed::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned on = color_ ? color_ : 0xff6fcf6a;
   // The glow fills the height, so the light sits inset from the left edge by the glow's radius and
   // nothing is clipped. Same layers as the envelope editor's playheads: two soft halos, the light,
   // and a bright core, each scaled by the level.
   const float h = height(), c = h * 0.5f, glow = h, d = h * 0.46f;
   const auto alpha = [&](float a) { return static_cast<unsigned>(a * level_) << 24; };
   if (level_ > 0.0f) {
      canvas.setColor((on & 0x00ffffff) | alpha(0x30));
      canvas.circle(c - glow * 0.5f, c - glow * 0.5f, glow);
      canvas.setColor((on & 0x00ffffff) | alpha(0x60));
      canvas.circle(c - glow * 0.32f, c - glow * 0.32f, glow * 0.64f);
   }
   canvas.setColor(blend(t.knobDark, on, level_));
   canvas.circle(c - d * 0.5f, c - d * 0.5f, d);
   // The rim shows the light while it is dark, and fades out as it lights.
   canvas.setColor((t.panelBorder & 0x00ffffff) |
                   (static_cast<unsigned>(((t.panelBorder >> 24) & 0xff) * (1.0f - level_)) << 24));
   canvas.ring(c - d * 0.5f, c - d * 0.5f, d, 1.0f);
   if (level_ > 0.0f) {
      canvas.setColor((blend(on, 0xffffffff, 0.6f) & 0x00ffffff) | alpha(0xff));
      canvas.circle(c - d * 0.22f, c - d * 0.22f, d * 0.44f);
   }
   if (!label_.empty()) {
      canvas.setColor(t.dimText);
      canvas.text(label_, font(h * 0.55f), visage::Font::kLeft, h + 2.0f, 0, std::max(0.0f, width() - h - 2.0f), h);
   }
}

} // namespace jclap::ui
