#pragma once

// An isomorphic note grid in the LinnStrument layout: each column is a semitone up, each row a
// fourth (5 semitones) up, lowest notes at the bottom left. Notes in the scale are lit, roots more.
//
// It also shows per-note expression (MPE): each held note gets a marker that slides sideways with
// its pitch bend (one cell per semitone, with a trail back to the key), rises with timbre (CC 74 /
// brightness) and grows with pressure. Give it the voices to show with setTouches().
//
// Playing it: press a cell for a note, slide sideways to bend, up and down for timbre. The
// callbacks fire as you go, so a plugin can turn them into notes and note expressions.

#include <cstdint>
#include <functional>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class NoteGrid : public visage::Frame {
public:
   struct Touch {
      int note = 60;
      float bend = 0.0f;     // semitones
      float pressure = 0.0f; // 0..1
      float timbre = 0.5f;   // 0..1
   };

   explicit NoteGrid(int columns = 16, int rows = 8, int lowNote = 30, int rowInterval = 5);

   void setLayout(int columns, int rows, int lowNote, int rowInterval = 5);
   // Pitch classes in the scale as a 12-bit mask (bit 0 = C), and the root. Defaults to C major.
   void setScale(uint16_t mask, int root);
   // How far a full slide across one cell bends, in semitones. 1 by default, like a LinnStrument.
   void setBendPerCell(float semitones);

   // The voices to show (held notes and their expression). Replaces the previous set.
   void setTouches(std::vector<Touch> touches);
   // Lets go of a cell held with the mouse, sending onNoteOff. For a panel closing mid-press, so the
   // note doesn't hang.
   void releaseNote();

   int noteAt(int column, int row) const { return low_ + column + row * interval_; }

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   std::function<void(int note, float velocity)> onNoteOn;
   // While a note is held from the grid: its bend in semitones, pressure and timbre.
   std::function<void(int note, float bend, float pressure, float timbre)> onExpression;
   std::function<void(int note)> onNoteOff;

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;

private:
   float cellWidth() const { return width() / columns_; }
   float cellHeight() const { return height() / rows_; }
   void drawTouch(visage::Canvas &canvas, const Touch &touch, unsigned color);
   Touch touchFrom(visage::Point p) const;

   const Theme *theme_ = &defaultTheme();
   int columns_ = 16, rows_ = 8, low_ = 30, interval_ = 5;
   uint16_t scale_ = 0xAB5; // C D E F G A B
   int root_ = 0;
   float bendPerCell_ = 1.0f;
   unsigned accent_ = 0;
   std::vector<Touch> touches_;

   // The note being played from the grid, and where it started.
   bool playing_ = false;
   Touch own_;
   int ownColumn_ = 0, ownRow_ = 0;
   visage::Point start_;
};

} // namespace jclap::ui
