#pragma once

// Colours for animations that draw every frame.
//
// Visage keeps every distinct colour (and gradient) a frame draws in its gradient atlas: a new one
// costs a map insert, an atlas slot and a texture upload of its own, and is thrown away a frame after
// it was last drawn. A fade or a twinkle nudges its colour a little every frame, so a busy drawing
// makes hundreds of new ones each frame, and that, not the shapes, is what slows it down. steady()
// rounds a colour to a fixed grid, finer than the eye can see on a soft glow, so the same colours
// come round again frame after frame and Visage reuses them.

#include <algorithm>
#include <cmath>

#include <visage/graphics.h>

namespace jclap::ui {

inline visage::Color steady(const visage::Color &c) {
   // Steps of 1/48 per colour channel and 1/16 for the HDR boost. Alpha is stepped on its square root
   // (48 steps), so faint glows, where a step shows most, get the finest ones.
   const auto round = [](float v, float steps) { return std::round(v * steps) / steps; };
   const float a = round(std::sqrt(std::max(c.alpha(), 0.0f)), 48.0f);
   return {a * a, round(c.red(), 48.0f), round(c.green(), 48.0f), round(c.blue(), 48.0f),
           round(c.hdr(), 16.0f)};
}

// A soft radial glow from `inner` at `center` to transparent at `radius`, in steady colours.
inline visage::Brush steadyGlow(const visage::Color &inner, visage::Point center, float radius) {
   const visage::Color c = steady(inner);
   return visage::Brush::radial(c, c.withAlpha(0.0f), center, radius);
}

} // namespace jclap::ui
