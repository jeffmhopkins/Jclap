#pragma once

// A piano keyboard. It lights the keys you tell it are held (setNoteOn, e.g. from notes the plugin
// received) and plays notes when clicked: onNoteOn / onNoteOff fire as you press, glide across and
// release keys. Velocity comes from how far down the key you click.

#include <bitset>
#include <functional>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace jclap::ui {

class PianoKeyboard : public visage::Frame {
public:
   // The range shown, as MIDI note numbers. It is widened to start and end on white keys.
   explicit PianoKeyboard(int lowNote = 48, int highNote = 72);

   void setRange(int lowNote, int highNote);
   int lowNote() const { return low_; }
   int highNote() const { return high_; }

   // Keys held from outside (host notes); drawn in the accent colour.
   void setNoteOn(int note, bool on);
   void clearNotes();
   // Lets go of a key held with the mouse, sending onNoteOff. For a panel closing mid-press, so the
   // note doesn't hang.
   void releaseNote() { release(); }
   bool isNoteOn(int note) const { return note >= 0 && note < 128 && held_[note]; }

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   // Velocity is 0..1.
   std::function<void(int note, float velocity)> onNoteOn;
   std::function<void(int note)> onNoteOff;

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;

   static bool isBlack(int note);

private:
   int whiteCount() const;
   // Left edge and width of a key, in local coordinates.
   void keyBounds(int note, float &x, float &w) const;
   int noteAt(visage::Point p) const;
   void press(int note, float velocity);
   void release();

   const Theme *theme_ = &defaultTheme();
   int low_ = 48, high_ = 72;
   std::bitset<128> held_;
   int playing_ = -1;
   unsigned accent_ = 0;
};

} // namespace jclap::ui
