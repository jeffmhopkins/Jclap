#include "jclap/ui/note_grid.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace jclap::ui {
namespace {

constexpr unsigned kRoot = 0xff3f6d9e;
constexpr unsigned kInScale = 0xff4d545e;
constexpr unsigned kOffScale = 0xff2b3038;

int pitchClass(int note) { return ((note % 12) + 12) % 12; }

std::string noteName(int note) {
   static const char *const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
   return kNames[pitchClass(note)] + std::to_string(note / 12 - 1);
}

} // namespace

NoteGrid::NoteGrid(int columns, int rows, int lowNote, int rowInterval) {
   setLayout(columns, rows, lowNote, rowInterval);
}

void NoteGrid::setLayout(int columns, int rows, int lowNote, int rowInterval) {
   columns_ = std::max(1, columns);
   rows_ = std::max(1, rows);
   low_ = lowNote;
   interval_ = rowInterval;
   redraw();
}

void NoteGrid::setScale(uint16_t mask, int root) {
   scale_ = mask;
   root_ = pitchClass(root);
   redraw();
}

void NoteGrid::setBendPerCell(float semitones) {
   bendPerCell_ = std::max(0.01f, semitones);
   redraw();
}

void NoteGrid::setTouches(std::vector<Touch> touches) {
   const auto same = [](const Touch &a, const Touch &b) {
      return a.note == b.note && a.bend == b.bend && a.pressure == b.pressure && a.timbre == b.timbre;
   };
   if (touches.size() == touches_.size() && std::equal(touches.begin(), touches.end(), touches_.begin(), same))
      return;
   touches_ = std::move(touches);
   redraw();
}

void NoteGrid::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void NoteGrid::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void NoteGrid::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float cw = cellWidth(), ch = cellHeight(), gap = std::max(1.0f, std::min(cw, ch) * 0.06f);
   const float r = std::min(cw, ch) * 0.12f;

   // Which notes are held, to light every cell that plays them.
   std::vector<bool> held(128, false);
   for (const Touch &touch : touches_)
      if (touch.note >= 0 && touch.note < 128)
         held[touch.note] = true;
   if (playing_ && own_.note >= 0 && own_.note < 128)
      held[own_.note] = true;

   for (int row = 0; row < rows_; ++row) {
      for (int col = 0; col < columns_; ++col) {
         const int note = noteAt(col, row);
         const int pc = pitchClass(note);
         const float x = col * cw, y = height() - (row + 1) * ch;
         unsigned color = kOffScale;
         if (note >= 0 && note < 128 && held[note])
            color = (accent & 0x00ffffff) | 0x99000000;
         else if (pc == root_)
            color = kRoot;
         else if (scale_ >> pc & 1)
            color = kInScale;
         canvas.setColor(color);
         canvas.roundedRectangle(x + gap * 0.5f, y + gap * 0.5f, cw - gap, ch - gap, r);
         if (pc == root_ && note >= 0) {
            canvas.setColor(t.text);
            canvas.text(noteName(note), font(std::min(ch * 0.3f, cw * 0.32f)), visage::Font::kCenter, x, y, cw, ch);
         }
      }
   }

   for (const Touch &touch : touches_)
      drawTouch(canvas, touch, accent);
   if (playing_)
      drawTouch(canvas, own_, t.text);
}

void NoteGrid::drawTouch(visage::Canvas &canvas, const Touch &touch, unsigned color) {
   const Theme &t = *theme_;
   const float cw = cellWidth(), ch = cellHeight();
   const float size = std::min(cw, ch) * (0.3f + 0.45f * std::clamp(touch.pressure, 0.0f, 1.0f));
   // Every cell that plays this note shows the touch, as on a LinnStrument.
   for (int row = 0; row < rows_; ++row) {
      const int col = touch.note - low_ - row * interval_;
      if (col < 0 || col >= columns_)
         continue;
      const float cx = (col + 0.5f) * cw, top = height() - (row + 1) * ch;
      const float x = std::clamp(cx + touch.bend / bendPerCell_ * cw, 0.0f, width());
      const float y = top + (1.0f - std::clamp(touch.timbre, 0.0f, 1.0f)) * ch;
      // The bend's trail back to the key's centre.
      if (std::abs(x - cx) > 0.5f) {
         canvas.setColor((color & 0x00ffffff) | 0x88000000);
         canvas.segment(cx, y, x, y, std::max(2.0f, ch * 0.08f), true);
      }
      canvas.setColor(t.shadow);
      canvas.circle(x - size * 0.5f, y - size * 0.5f + 1.0f, size);
      canvas.setColor(color);
      canvas.circle(x - size * 0.5f, y - size * 0.5f, size);
   }
}

NoteGrid::Touch NoteGrid::touchFrom(visage::Point p) const {
   const float cw = cellWidth(), ch = cellHeight();
   Touch touch = own_;
   touch.bend = (p.x - start_.x) / cw * bendPerCell_;
   const float top = height() - (ownRow_ + 1) * ch;
   touch.timbre = std::clamp(1.0f - (p.y - top) / ch, 0.0f, 1.0f);
   return touch;
}

void NoteGrid::mouseDown(const visage::MouseEvent &e) {
   const int col = static_cast<int>(e.position.x / cellWidth());
   const int row = static_cast<int>((height() - e.position.y) / cellHeight());
   if (col < 0 || col >= columns_ || row < 0 || row >= rows_)
      return;
   const int note = noteAt(col, row);
   if (note < 0 || note > 127)
      return;
   playing_ = true;
   ownColumn_ = col;
   ownRow_ = row;
   // Bends are measured from the cell's centre, so a press off-centre doesn't start detuned.
   start_ = {(col + 0.5f) * cellWidth(), e.position.y};
   own_ = Touch{note, 0.0f, 0.8f, 0.5f};
   own_ = touchFrom({start_.x, e.position.y});
   if (onNoteOn)
      onNoteOn(note, 0.8f);
   if (onExpression)
      onExpression(note, own_.bend, own_.pressure, own_.timbre);
   redraw();
}

void NoteGrid::mouseDrag(const visage::MouseEvent &e) {
   if (!playing_)
      return;
   own_ = touchFrom(e.position);
   if (onExpression)
      onExpression(own_.note, own_.bend, own_.pressure, own_.timbre);
   redraw();
}

void NoteGrid::mouseUp(const visage::MouseEvent &) {
   if (!playing_)
      return;
   playing_ = false;
   if (onNoteOff)
      onNoteOff(own_.note);
   redraw();
}

} // namespace jclap::ui
