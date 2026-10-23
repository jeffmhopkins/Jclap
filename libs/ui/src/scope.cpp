#include "jclap/ui/scope.h"

#include <algorithm>

namespace jclap::ui {

void Scope::setSamples(const float *samples, size_t count) {
   // A held or silent signal gives the same samples tick after tick; skip the redraw then.
   if (samples_.size() == count && std::equal(samples, samples + count, samples_.begin()))
      return;
   samples_.assign(samples, samples + count);
   redraw();
}

void Scope::setScale(float amplitude) {
   scale_ = std::max(1e-6f, amplitude);
   redraw();
}

void Scope::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void Scope::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void Scope::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float w = width(), h = height(), r = std::min(w, h) * 0.04f;
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(0, 0, w, h, r);
   canvas.setColor(t.track);
   for (int i = 1; i < 8; ++i)
      canvas.fill(w * i / 8.0f, 0, 1.0f, h);
   canvas.fill(0, h * 0.25f, w, 1.0f);
   canvas.fill(0, h * 0.75f, w, 1.0f);
   canvas.setColor(t.panelBorder);
   canvas.fill(0, h * 0.5f, w, 1.0f);
   canvas.roundedRectangleBorder(0, 0, w, h, r, 1.0f);

   const size_t n = samples_.size();
   if (n < 2)
      return;
   const auto y = [&](float s) { return h * 0.5f - std::clamp(s / scale_, -1.0f, 1.0f) * (h * 0.5f - 2.0f); };
   canvas.setColor(accent_ ? accent_ : t.accent);
   const int columns = std::max(1, static_cast<int>(w));
   if (n <= static_cast<size_t>(columns)) {
      // Fewer samples than pixels: join the dots.
      for (size_t i = 0; i + 1 < n; ++i)
         canvas.segment(w * i / (n - 1), y(samples_[i]), w * (i + 1) / (n - 1), y(samples_[i + 1]), 1.5f, true);
      return;
   }
   // More samples than pixels: a min-max bar per column.
   for (int c = 0; c < columns; ++c) {
      // Each column also takes the previous column's last sample, so steep edges stay joined.
      const size_t start = n * c / columns, from = start > 0 ? start - 1 : 0;
      const size_t to = std::max(start + 1, n * (c + 1) / columns);
      const auto [lo, hi] = std::minmax_element(samples_.begin() + from, samples_.begin() + to);
      const float top = y(*hi), bottom = y(*lo);
      canvas.fill(static_cast<float>(c), top, 1.0f, std::max(1.5f, bottom - top));
   }
}

} // namespace jclap::ui
