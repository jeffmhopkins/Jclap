#pragma once

// A level meter with a falling peak marker. Feed it from a timer with the latest level, usually a
// value the audio thread stores in an atomic. Green up to -12 dB, orange to -3 dB, red above.

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Meter : public visage::Frame {
public:
   enum class Orientation { Vertical, Horizontal };

   explicit Meter(Orientation orientation = Orientation::Vertical) : orientation_(orientation) {
      setIgnoresMouseEvents(true, false);
   }

   // Linear amplitude (1.0 = 0 dB).
   void setLevel(float amplitude);
   void setLevelDb(float db);
   float levelDb() const { return levelDb_; }

   // The meter's scale. Defaults to -60 dB .. +6 dB.
   void setRange(float minDb, float maxDb);
   // How fast the peak marker falls, in dB per setLevel call.
   float peakFallDb = 0.5f;

   void setTheme(const Theme &theme);
   void draw(visage::Canvas &canvas) override;

private:
   float fraction(float db) const;

   const Theme *theme_ = &defaultTheme();
   Orientation orientation_;
   float minDb_ = -60.0f, maxDb_ = 6.0f;
   float levelDb_ = -200.0f, peakDb_ = -200.0f;
};

} // namespace jclap::ui
