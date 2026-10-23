#include "jclap/ui/keyboard.h"

#include <algorithm>

namespace jclap::ui {
namespace {

constexpr float kBlackHeight = 0.62f;
constexpr float kBlackWidth = 0.6f;

// White keys before `note` counting from note 0.
int whitesBefore(int note) {
   static const int kInOctave[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
   return note / 12 * 7 + kInOctave[note % 12];
}

} // namespace

bool PianoKeyboard::isBlack(int note) {
   const int n = ((note % 12) + 12) % 12;
   return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

PianoKeyboard::PianoKeyboard(int lowNote, int highNote) { setRange(lowNote, highNote); }

void PianoKeyboard::setRange(int lowNote, int highNote) {
   low_ = std::clamp(std::min(lowNote, highNote), 0, 127);
   high_ = std::clamp(std::max(lowNote, highNote), 0, 127);
   if (isBlack(low_))
      --low_;
   if (isBlack(high_))
      ++high_;
   redraw();
}

bool PianoKeyboard::shiftOctaves(int octaves) {
   // Whole octaves only, as far as the range still fits.
   while (octaves > 0 && high_ + 12 * octaves > 127)
      --octaves;
   while (octaves < 0 && low_ + 12 * octaves < 0)
      ++octaves;
   if (octaves == 0)
      return false;
   release();
   setRange(low_ + 12 * octaves, high_ + 12 * octaves);
   return true;
}

void PianoKeyboard::setNoteOn(int note, bool on) {
   if (note < 0 || note > 127 || held_[note] == on)
      return;
   held_[note] = on;
   redraw();
}

void PianoKeyboard::clearNotes() {
   held_.reset();
   redraw();
}

void PianoKeyboard::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void PianoKeyboard::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

int PianoKeyboard::whiteCount() const { return whitesBefore(high_) - whitesBefore(low_) + 1; }

void PianoKeyboard::keyBounds(int note, float &x, float &w) const {
   const float white = width() / std::max(1, whiteCount());
   const float left = (whitesBefore(note) - whitesBefore(low_)) * white;
   if (isBlack(note)) {
      w = white * kBlackWidth;
      x = left - w * 0.5f;
   }
   else {
      w = white;
      x = left;
   }
}

int PianoKeyboard::noteAt(visage::Point p) const {
   if (p.x < 0 || p.x >= width() || p.y < 0 || p.y >= height())
      return -1;
   // Black keys sit on top, so check them first.
   if (p.y < height() * kBlackHeight) {
      for (int n = low_; n <= high_; ++n) {
         float x, w;
         if (isBlack(n) && (keyBounds(n, x, w), p.x >= x && p.x < x + w))
            return n;
      }
   }
   for (int n = low_; n <= high_; ++n) {
      float x, w;
      if (!isBlack(n) && (keyBounds(n, x, w), p.x >= x && p.x < x + w))
         return n;
   }
   return -1;
}

void PianoKeyboard::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float h = height(), r = std::max(2.0f, h * 0.04f);
   for (int pass = 0; pass < 2; ++pass) {
      for (int n = low_; n <= high_; ++n) {
         if (isBlack(n) != (pass == 1))
            continue;
         float x, w;
         keyBounds(n, x, w);
         const bool lit = held_[n] || n == playing_;
         if (pass == 0) {
            canvas.setColor(lit ? accent : 0xffd9dce0);
            canvas.roundedRectangle(x + 0.5f, -r, w - 1.0f, h + r - 1.0f, r);
            if (n % 12 == 0) {
               canvas.setColor(t.dimText);
               canvas.text("C" + std::to_string(n / 12 - 1), font(std::min(w * 0.42f, h * 0.16f)), visage::Font::kCenter,
                           x, h * 0.78f, w, h * 0.18f);
            }
         }
         else {
            canvas.setColor(lit ? accent : t.knobEdge);
            canvas.roundedRectangle(x, -r, w, h * kBlackHeight + r, r);
         }
      }
   }
   canvas.setColor(t.panelBorder);
   canvas.fill(0, 0, width(), 2.0f);

   // An arrow at each edge past which a note is held.
   bool below = false, above = false;
   for (int n = 0; n < 128; ++n) {
      if (held_[n]) {
         below |= n < low_;
         above |= n > high_;
      }
   }
   const float a = std::min(h * 0.22f, width() * 0.05f), y = h * kBlackHeight + (h * (1.0f - kBlackHeight) - a) * 0.5f;
   for (int side = 0; side < 2; ++side) {
      if (!(side == 0 ? below : above))
         continue;
      const float x = side == 0 ? 3.0f : width() - 3.0f - a * 0.5f;
      canvas.setColor(t.shadow);
      canvas.roundedRectangle(x - 3.0f, y - 3.0f, a * 0.5f + 6.0f, a + 6.0f, 3.0f);
      canvas.setColor(accent);
      if (side == 0)
         canvas.triangleLeft(x, y, a * 0.5f);
      else
         canvas.triangleRight(x, y, a * 0.5f);
   }
}

void PianoKeyboard::press(int note, float velocity) {
   if (note == playing_)
      return;
   release();
   if (note < 0)
      return;
   playing_ = note;
   if (onNoteOn)
      onNoteOn(note, velocity);
   redraw();
}

void PianoKeyboard::release() {
   if (playing_ < 0)
      return;
   const int note = playing_;
   playing_ = -1;
   if (onNoteOff)
      onNoteOff(note);
   redraw();
}

void PianoKeyboard::mouseDown(const visage::MouseEvent &e) {
   const int note = noteAt(e.position);
   const float keyHeight = isBlack(note) ? height() * kBlackHeight : height();
   press(note, std::clamp(e.position.y / keyHeight, 0.1f, 1.0f));
}

void PianoKeyboard::mouseDrag(const visage::MouseEvent &e) {
   const int note = noteAt(e.position);
   if (note != playing_)
      press(note, 0.8f);
}

void PianoKeyboard::mouseUp(const visage::MouseEvent &) { release(); }

} // namespace jclap::ui
