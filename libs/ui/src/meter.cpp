#include "jclap/ui/meter.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {
namespace {

constexpr unsigned kGreen = 0xff6fcf6a;
constexpr unsigned kOrange = 0xfff2a33a;
constexpr unsigned kRed = 0xffe5534b;

} // namespace

void Meter::setLevel(float amplitude) {
   setLevelDb(amplitude > 0.0f ? 20.0f * std::log10(amplitude) : -200.0f);
}

void Meter::setLevelDb(float db) {
   const float peak = std::max(db, peakDb_ - peakFallDb);
   if (db != levelDb_ || peak != peakDb_) {
      levelDb_ = db;
      peakDb_ = peak;
      redraw();
   }
}

void Meter::setRange(float minDb, float maxDb) {
   minDb_ = minDb;
   maxDb_ = maxDb;
   redraw();
}

void Meter::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

float Meter::fraction(float db) const { return std::clamp((db - minDb_) / (maxDb_ - minDb_), 0.0f, 1.0f); }

void Meter::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const bool vertical = orientation_ == Orientation::Vertical;
   const float w = width(), h = height();
   const float length = vertical ? h : w;
   const float r = std::min(w, h) * 0.2f;
   canvas.setColor(t.track);
   canvas.roundedRectangle(0, 0, w, h, r);

   // Draws the part of the scale between two dB values in one colour.
   const auto band = [&](float fromDb, float toDb, unsigned color) {
      const float a = fraction(fromDb) * length, b = fraction(std::min(toDb, levelDb_)) * length;
      if (b <= a)
         return;
      canvas.setColor(color);
      if (vertical)
         canvas.fill(0, h - b, w, b - a);
      else
         canvas.fill(a, 0, b - a, h);
   };
   band(minDb_, -12.0f, kGreen);
   band(-12.0f, -3.0f, kOrange);
   band(-3.0f, maxDb_, kRed);

   if (peakDb_ > minDb_) {
      const float p = fraction(peakDb_) * length;
      canvas.setColor(peakDb_ > -3.0f ? kRed : t.text);
      if (vertical)
         canvas.fill(0, h - p, w, 2.0f);
      else
         canvas.fill(p - 2.0f, 0, 2.0f, h);
   }
   // 0 dB tick.
   canvas.setColor(t.panelBorder);
   const float zero = fraction(0.0f) * length;
   if (vertical)
      canvas.fill(0, h - zero, w, 1.0f);
   else
      canvas.fill(zero, 0, 1.0f, h);
}

} // namespace jclap::ui
