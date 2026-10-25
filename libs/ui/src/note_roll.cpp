#include "jclap/ui/note_roll.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace jclap::ui {

void NoteRoll::setSteps(int steps, int stepsPerBeat) {
   steps_ = std::max(1, steps);
   stepsPerBeat_ = std::max(1, stepsPerBeat);
   redraw();
}

void NoteRoll::setNotes(const std::vector<Note> &notes) {
   const auto same = [](const Note &a, const Note &b) {
      return a.start == b.start && a.length == b.length && a.pitch == b.pitch && a.velocity == b.velocity;
   };
   if (notes.size() == notes_.size() && std::equal(notes.begin(), notes.end(), notes_.begin(), same))
      return;
   notes_ = notes;
   redraw();
}

void NoteRoll::setPlayhead(float step) {
   if (step == playhead_)
      return;
   playhead_ = step;
   redraw();
}

void NoteRoll::setPitchRange(int low, int high) {
   low_ = std::min(low, high);
   high_ = std::max(low, high);
   redraw();
}

void NoteRoll::setCompact(bool compact) {
   compact_ = compact;
   redraw();
}

void NoteRoll::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void NoteRoll::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void NoteRoll::setLanes(std::vector<int> pitches) {
   std::sort(pitches.begin(), pitches.end(), std::greater<int>());
   pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
   if (pitches == fixedLanes_)
      return;
   fixedLanes_ = std::move(pitches);
   redraw();
}

std::vector<int> NoteRoll::lanes() const {
   if (!fixedLanes_.empty())
      return fixedLanes_;
   std::vector<int> lanes;
   if (compact_) {
      for (const Note &n : notes_)
         if (std::find(lanes.begin(), lanes.end(), n.pitch) == lanes.end())
            lanes.push_back(n.pitch);
      std::sort(lanes.begin(), lanes.end(), std::greater<int>());
      return lanes;
   }
   int low = low_, high = high_;
   if (high < low) {
      if (notes_.empty())
         return lanes;
      low = high = notes_.front().pitch;
      for (const Note &n : notes_) {
         low = std::min(low, n.pitch);
         high = std::max(high, n.pitch);
      }
      low -= 2;
      high += 2;
   }
   for (int p = high; p >= low; --p)
      lanes.push_back(p);
   return lanes;
}

void NoteRoll::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float w = width(), h = height(), r = std::min(w, h) * 0.04f;
   canvas.setColor(t.knobDark);
   canvas.roundedRectangle(0, 0, w, h, r);

   const float stepW = w / steps_;
   for (int i = 1; i < steps_; ++i) {
      canvas.setColor(i % stepsPerBeat_ == 0 ? t.panelBorder : t.track);
      canvas.fill(stepW * i, 0, 1.0f, h);
   }

   const std::vector<int> rows = lanes();
   const float pad = std::max(2.0f, h * 0.04f);
   const float laneH = rows.empty() ? h : (h - 2 * pad) / rows.size();
   const auto laneOf = [&](int pitch) {
      for (size_t i = 0; i < rows.size(); ++i)
         if (rows[i] == pitch)
            return static_cast<int>(i);
      return -1;
   };

   for (const Note &n : notes_) {
      const int lane = laneOf(n.pitch);
      if (lane < 0)
         continue;
      const float x = n.start * stepW + 1.0f, bw = std::max(2.0f, n.length * stepW - 2.0f);
      const float bh = std::max(2.0f, std::min(laneH - 2.0f, h * 0.3f));
      const float y = pad + lane * laneH + (laneH - bh) * 0.5f;
      const bool lit = playhead_ >= n.start && playhead_ < n.start + n.length;
      const float v = std::clamp(n.velocity, 0.15f, 1.0f);
      // Quiet notes fade toward the background; the lit ones get a soft glow under them.
      const unsigned alpha = static_cast<unsigned>((lit ? 1.0f : 0.45f + 0.45f * v) * 255.0f);
      if (lit) {
         canvas.setColor((accent & 0x00ffffffu) | 0x40000000u);
         canvas.roundedRectangle(x - 3, y - 3, bw + 6, bh + 6, bh * 0.5f + 3);
      }
      canvas.setColor((accent & 0x00ffffffu) | (alpha << 24));
      canvas.roundedRectangle(x, y, bw, bh, std::min(bh * 0.3f, 3.0f));
      if (bh >= 9.0f && bw >= bh * 1.6f) {
         canvas.setColor(t.knobDark);
         canvas.text(noteName(n.pitch), font(bh * 0.72f), visage::Font::kLeft, x + bh * 0.3f, y, bw, bh);
      }
   }

   if (playhead_ >= 0.0f && playhead_ <= steps_) {
      canvas.setColor(t.text);
      canvas.fill(playhead_ * stepW, 0, 1.5f, h);
   }
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, w, h, r, 1.0f);
}

} // namespace jclap::ui
