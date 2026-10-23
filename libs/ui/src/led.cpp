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
   const float h = height(), d = h * 0.6f, y = (h - d) * 0.5f;
   if (level_ > 0.0f) {
      // A soft glow behind the light while it is lit.
      canvas.setColor((on & 0x00ffffff) | (static_cast<unsigned>(0x60 * level_) << 24));
      canvas.circle(-d * 0.25f, y - d * 0.25f, d * 1.5f);
   }
   canvas.setColor(blend(t.knobDark, on, level_));
   canvas.circle(d * 0.25f, y, d);
   canvas.setColor(t.panelBorder);
   canvas.ring(d * 0.25f, y, d, 1.0f);
   if (!label_.empty()) {
      canvas.setColor(t.dimText);
      canvas.text(label_, font(h * 0.55f), visage::Font::kLeft, d * 1.6f, 0, width() - d * 1.6f, h);
   }
}

} // namespace jclap::ui
