#pragma once

// Draws a stretch of audio: an oscilloscope for a live signal, or a static waveform. Feed it from
// a timer with setSamples(), usually read from a jclap::core::SampleFifo the audio thread writes.
// When there are more samples than pixels it draws each column's min and max, so peaks never vanish.

#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class Scope : public visage::Frame {
public:
   Scope() { setIgnoresMouseEvents(true, false); }

   void setSamples(const float *samples, size_t count);
   void setSamples(const std::vector<float> &samples) { setSamples(samples.data(), samples.size()); }
   // The amplitude at the top and bottom edges. Defaults to 1.
   void setScale(float amplitude);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);
   void draw(visage::Canvas &canvas) override;

private:
   const Theme *theme_ = &defaultTheme();
   std::vector<float> samples_;
   float scale_ = 1.0f;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
