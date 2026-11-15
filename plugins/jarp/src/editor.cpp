// JArp's panel, in Polar XY's plain style: a title bar with the presets and the dice, the PATTERN
// card (the grid of eight rows by sixteen steps, with the Octave, Ratchet and Velocity lanes under
// it) and the settings card (Timing, Playback, Pressure), all on a 900 x 743 design grid scaled to
// the window.
//
// Colour carries meaning: orange is notes, timing and the row playing; blue is pressure; purple is
// only the pattern's end and the slide bar; grey is labels and selected switches.
//
// The grid: click an empty cell for a one-step note and drag right to lengthen it as you add it;
// drag a note's right edge to set how long it sounds (quarter steps, stopping at the next note in
// the row); click a note to remove it. Drag the purple bar (or its tab) to set the pattern's length.
// Each row's label shows the held note it plays, with that finger's pressure, slide and bend as
// three hairlines along its bottom.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "params.h"
#include "presets.h"

namespace jarp {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = kEditorWidth;
constexpr float kDesignHeight = kEditorHeight;
constexpr float kHeaderHeight = 64.0f;

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kSlate = 0xff454c58;
constexpr unsigned kCell = 0xff272c34;
constexpr unsigned kCellOff = 0xff1f232a;
constexpr unsigned kRule = 0xff2c323b;

// The grid's geometry, in design units within its view: where the first step's cells start, the
// step and row pitch, and a cell's size.
constexpr float kGridX = 135.0f, kGridY = 27.0f;
constexpr float kColumn = 45.0f, kCellW = 40.0f;
constexpr float kRowPitch = 34.0f, kCellH = 29.0f;

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

visage::Color alpha(unsigned argb, float a) { return visage::Color(argb).withAlpha(a); }

// A note's width for a length in quarter steps: a whole step is a cell, and longer ones span the
// gaps between cells.
float noteWidth(int quarters) {
   return quarters * kColumn / kQuarters - (kColumn - kCellW) * std::min(1.0f, quarters / static_cast<float>(kQuarters));
}

const char *ordinal(int row) {
   static const char *const kNames[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
   return kNames[std::clamp(row, 0, 7)];
}

// The grid of notes, with the row labels at its left, the step numbers under it and the pattern's
// end bar.
class GridView : public visage::Frame {
public:
   explicit GridView(EditorHost &host) : host_(host) {}

   std::function<void(const Pattern &)> onEdit;

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Pattern &p = host_.pattern();
      const jarp::Frame &f = host_.frame();
      const int len = length();
      const bool degree = std::lround(host_.paramValue(kRowsBy)) != 0;
      const int playStep = f.running ? std::clamp(static_cast<int>(f.position), 0, len - 1) : -1;
      const int playQuarter = f.running ? static_cast<int>(f.position * kQuarters) : -1;

      // The playhead's column, behind the cells.
      if (playStep >= 0) {
         const float x = (kGridX + playStep * kColumn - 3) * s;
         c.setColor(0xff4a515c);
         c.roundedRectangleBorder(x, (kGridY - 8) * s, (kCellW + 6) * s, (7 * kRowPitch + kCellH + 16) * s, 5 * s, 1.2f * s);
      }

      for (int r = 0; r < kRows; ++r) {
         const float y = rowY(r);
         for (int col = 0; col < kSteps; ++col) {
            c.setColor(col < len ? kCell : kCellOff);
            c.roundedRectangle((kGridX + col * kColumn) * s, y * s, kCellW * s, kCellH * s, 3 * s);
         }
      }

      // Notes, longest first so short ones stay on top where they cross nothing anyway.
      for (int i = 0; i < p.count; ++i) {
         const Note &n = p.notes[i];
         const Lane &lane = p.lanes[n.step];
         const bool live = n.step < len;
         const float x = kGridX + n.step * kColumn, y = rowY(n.row);
         const int quarters = std::min<int>(n.length, live ? (len - n.step) * kQuarters : n.length);
         const float w = noteWidth(quarters);
         const bool sounding = live && f.running && f.rows[n.row].key >= 0 && playQuarter >= n.step * kQuarters &&
                               playQuarter < n.step * kQuarters + quarters;
         const float a = live ? 1.0f : 0.3f;
         const int base = lane.ratchet;
         const int extra = lane.pressure ? std::max(0, maxHits() - base) : 0;
         if (base <= 1 && !extra) {
            c.setColor(alpha(sounding ? 0xffffa94d : kOrange, a));
            c.roundedRectangle(x * s, y * s, w * s, kCellH * s, 3 * s);
         }
         else {
            // Ratchets: the note splits into its hits, the extra slots pressure can fill dashed in
            // blue and filled as it does.
            c.setColor(alpha(0xff3b2a1a, a));
            c.roundedRectangle(x * s, y * s, w * s, kCellH * s, 3 * s);
            c.setColor(alpha(sounding ? 0xffffffff : kOrange, a));
            c.roundedRectangleBorder(x * s, y * s, w * s, kCellH * s, 3 * s, 1.2f * s);
            const int slots = base + extra;
            const int lit = lane.pressure && f.hits[n.step] ? std::clamp<int>(f.hits[n.step] - base, 0, extra) : 0;
            const float pad = 3.0f, gap = 2.0f;
            const float each = (w - 2 * pad - gap * (slots - 1)) / slots;
            for (int h = 0; h < slots; ++h) {
               const float hx = x + pad + h * (each + gap);
               if (h < base) {
                  c.setColor(alpha(kOrange, a));
                  c.roundedRectangle(hx * s, (y + pad) * s, each * s, (kCellH - 2 * pad - (extra ? 4 : 0)) * s, 1.5f * s);
               }
               else if (h < base + lit) {
                  c.setColor(alpha(kBlue, a));
                  c.roundedRectangle(hx * s, (y + pad) * s, each * s, (kCellH - 2 * pad - 4) * s, 1.5f * s);
               }
               else {
                  c.setColor(alpha(kBlue, 0.55f * a));
                  c.roundedRectangleBorder(hx * s, (y + pad) * s, each * s, (kCellH - 2 * pad - 4) * s, 1.5f * s, 1.0f * s);
               }
            }
            if (extra) {
               c.setColor(alpha(kBlue, a));
               c.fill((x + pad) * s, (y + kCellH - pad - 1.5f) * s, (w - 2 * pad) * s, 1.5f * s);
            }
         }
         if (lane.octave) {
            char o[8];
            std::snprintf(o, sizeof(o), "%+d", lane.octave);
            c.setColor(alpha(base <= 1 && !extra ? 0xff3b2a1a : kOrange, a));
            c.text(o, ui::font(8.5f * s), visage::Font::kRight, x * s, (y + 1) * s, (w - 4) * s, 11 * s);
         }
      }

      // Row labels: the row (or its degree), the held note it plays, its channel (or, where a degree
      // snapped, the note it wanted), and the finger's pressure, slide and bend as hairlines.
      for (int r = 0; r < kRows; ++r) {
         const RowView &v = f.rows[r];
         const float y = rowY(r);
         const bool held = v.key >= 0;
         const bool playing = held && rowPlaying(p, f, r, len, playQuarter);
         if (playing) {
            c.setColor(kOrange);
            c.circle(0.5f * s, (y + 8) * s, 4 * s);
         }
         c.setColor(held ? t.dimText : alpha(t.dimText, 0.55f));
         if (degree)
            c.text(ordinal(r), ui::font(10.5f * s), visage::Font::kLeft, 7 * s, y * s, 40 * s, 20 * s);
         else
            c.text(std::to_string(r + 1), ui::font(10.5f * s), visage::Font::kLeft, 10 * s, y * s, 20 * s, 20 * s);
         if (!held) {
            c.text("-", ui::font(10.5f * s), visage::Font::kLeft, 25 * s, y * s, 20 * s, 20 * s);
            continue;
         }
         c.setColor(playing ? kOrange : t.text);
         c.text(ui::noteName(v.key), ui::font(11.5f * s), visage::Font::kLeft, (degree ? 32 : 25) * s, y * s, 40 * s, 20 * s);
         c.setColor(t.dimText);
         const std::string note = v.target >= 0 ? "wants " + ui::noteName(v.target) : "ch " + std::to_string(v.channel + 1);
         c.text(note, ui::font(9.5f * s), visage::Font::kLeft, 68 * s, y * s, 50 * s, 20 * s);
         // Hairlines: pressure and slide from the left, bend from the middle.
         const float lx = 7, lw = 115, ly = y + 22;
         const float lines[3] = {v.pressure, v.slide, v.bend};
         const unsigned colours[3] = {kBlue, kPurple, kOrange};
         for (int k = 0; k < 3; ++k) {
            const float yy = (ly + k * 2.5f) * s;
            c.setColor(0xff2a2f37);
            c.fill(lx * s, yy, lw * s, 1.2f * s);
            c.setColor(colours[k]);
            if (k < 2)
               c.fill(lx * s, yy, std::clamp(lines[k], 0.0f, 1.0f) * lw * s, 1.2f * s);
            else {
               const float b = std::clamp(lines[k], -1.0f, 1.0f) * lw / 2, mid = lx + lw / 2;
               c.fill(std::min(mid, mid + b) * s, yy, std::max(1.0f, std::abs(b)) * s, 1.2f * s);
            }
         }
      }

      // Step numbers, the beats brighter.
      for (int col = 0; col < kSteps; ++col) {
         c.setColor(col >= len ? alpha(t.dimText, 0.45f) : col % 4 == 0 ? t.text : t.dimText);
         c.text(std::to_string(col + 1), ui::font(9.5f * s), visage::Font::kCenter, (kGridX + col * kColumn) * s,
                (kGridY + 7 * kRowPitch + kCellH + 6) * s, kCellW * s, 16 * s);
      }

      // The pattern's end: a purple bar with a tab to drag.
      const float bx = barX(len);
      c.setColor(kPurple);
      c.fill((bx - 1) * s, 14 * s, 2 * s, (kGridY + 7 * kRowPitch + kCellH - 14) * s);
      c.roundedRectangle((bx - 7) * s, 4 * s, 14 * s, 14 * s, 3 * s);
      c.setColor(0xff3a2d52);
      for (int k = -1; k <= 1; ++k)
         c.fill((bx + k * 2.5f - 0.5f) * s, 7.5f * s, 1.0f * s, 7 * s);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      const Pattern &p = host_.pattern();
      // The end bar, anywhere along it.
      if (std::abs(x - barX(length())) < 7 && y < kGridY + 8 * kRowPitch) {
         drag_ = Drag::End;
         host_.beginEdit(kLength);
         return;
      }
      const int col = static_cast<int>(std::floor((x - kGridX) / kColumn));
      const int row = 7 - static_cast<int>(std::floor((y - kGridY) / kRowPitch));
      if (col < 0 || col >= kSteps || row < 0 || row >= kRows || y < kGridY)
         return;
      const int quarter = static_cast<int>(std::floor((x - kGridX) / kColumn * kQuarters));
      const int at = p.noteAt(row, std::min(quarter, kMaxLength - 1));
      Pattern next = p;
      if (at >= 0) {
         const Note &n = p.notes[at];
         const float right = kGridX + n.step * kColumn + noteWidth(n.length);
         if (right - x < 8) {
            drag_ = Drag::Length;
            note_ = at;
            return;
         }
         next.remove(at);
         commit(next);
         return;
      }
      // A cell's gap belongs to the cell before it.
      if (!next.add(row, col, kQuarters))
         return;
      commit(next);
      drag_ = Drag::Length;
      note_ = next.count - 1;
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s;
      if (drag_ == Drag::End) {
         const int len = std::clamp(static_cast<int>(std::lround((x - kGridX + 2.5f) / kColumn)), 1, kSteps);
         if (len != length())
            host_.setParam(kLength, len);
         return;
      }
      if (drag_ != Drag::Length)
         return;
      Pattern next = host_.pattern();
      if (note_ < 0 || note_ >= next.count)
         return;
      Note &n = next.notes[note_];
      const float start = kGridX + n.step * kColumn;
      const int quarters = static_cast<int>(std::lround((x - start + 2.5f) / kColumn * kQuarters));
      n.length = static_cast<uint8_t>(std::clamp(quarters, 1, next.room(n.row, n.step, note_)));
      commit(next);
   }

   void mouseUp(const visage::MouseEvent &) override {
      if (drag_ == Drag::End)
         host_.endEdit(kLength);
      drag_ = Drag::None;
      note_ = -1;
   }

private:
   enum class Drag { None, Length, End };

   int length() const { return std::clamp(static_cast<int>(std::lround(host_.paramValue(kLength))), 1, kSteps); }
   int maxHits() const { return static_cast<int>(std::lround(host_.paramModulated(kMaxHits))); }
   static float rowY(int row) { return kGridY + (7 - row) * kRowPitch; }
   static float barX(int len) { return kGridX + len * kColumn - (kColumn - kCellW) / 2; }

   static bool rowPlaying(const Pattern &p, const jarp::Frame &f, int row, int len, int playQuarter) {
      if (!f.running)
         return false;
      for (int i = 0; i < p.count; ++i) {
         const Note &n = p.notes[i];
         if (n.row != row || n.step >= len)
            continue;
         const int end = std::min<int>(n.step * kQuarters + n.length, len * kQuarters);
         if (playQuarter >= n.step * kQuarters && playQuarter < end)
            return true;
      }
      return false;
   }

   void commit(const Pattern &p) {
      if (onEdit)
         onEdit(p);
      redraw();
   }

   EditorHost &host_;
   Drag drag_ = Drag::None;
   int note_ = -1;
};

// The lanes under the grid, a cell per step: Octave, Ratchet (with its P toggle) and Velocity.
// Drag a cell up or down (or scroll on it) to change it; double-click sets it back.
class LaneView : public visage::Frame {
public:
   explicit LaneView(EditorHost &host) : host_(host) {}

   std::function<void(const Pattern &)> onEdit;

   static constexpr float kLaneY[3] = {7.0f, 37.0f, 67.0f};
   static constexpr float kLaneH = 20.0f;

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Pattern &p = host_.pattern();
      const int len = std::clamp(static_cast<int>(std::lround(host_.paramValue(kLength))), 1, kSteps);
      const char *const names[] = {"OCTAVE", "RATCHET", "VELOCITY"};
      for (int lane = 0; lane < 3; ++lane) {
         c.setColor(t.dimText);
         c.text(names[lane], ui::font(10.5f * s), visage::Font::kLeft, 7 * s, kLaneY[lane] * s, 120 * s, kLaneH * s);
      }
      for (int col = 0; col < kSteps; ++col) {
         const Lane &l = p.lanes[col];
         const float a = col < len ? 1.0f : 0.35f;
         const float x = kGridX + col * kColumn;
         for (int lane = 0; lane < 3; ++lane) {
            const float y = kLaneY[lane];
            c.setColor(alpha(0xff181b20, a));
            c.roundedRectangle(x * s, y * s, kCellW * s, kLaneH * s, 3 * s);
            c.setColor(alpha(kRule, a));
            c.roundedRectangleBorder(x * s, y * s, kCellW * s, kLaneH * s, 3 * s, 1.0f);
         }
         // Octave.
         char buf[8];
         std::snprintf(buf, sizeof(buf), l.octave ? "%+d" : "%d", l.octave);
         c.setColor(alpha(l.octave ? kOrange : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, kLaneY[0] * s, kCellW * s, kLaneH * s);
         // Ratchet: the count, and P.
         std::snprintf(buf, sizeof(buf), "%d", l.ratchet);
         c.setColor(alpha(l.ratchet > 1 ? kOrange : l.pressure ? t.text : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, kLaneY[1] * s, 24 * s, kLaneH * s);
         const float px = x + 25, py = kLaneY[1] + 5;
         if (l.pressure) {
            c.setColor(alpha(kBlue, a));
            c.roundedRectangle(px * s, py * s, 10 * s, 10 * s, 2 * s);
            c.setColor(alpha(0xff1c2027, a));
         }
         else {
            c.setColor(alpha(t.dimText, 0.6f * a));
            c.roundedRectangleBorder(px * s, py * s, 10 * s, 10 * s, 2 * s, 1.0f);
         }
         c.text("P", ui::font(7.5f * s), visage::Font::kCenter, px * s, py * s, 10 * s, 10 * s);
         // Velocity, with a bar from the middle.
         std::snprintf(buf, sizeof(buf), l.velocity ? "%+d" : "%d", l.velocity);
         c.setColor(alpha(l.velocity ? t.text : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, (kLaneY[2] - 1) * s, kCellW * s, kLaneH * s);
         const float mid = x + kCellW / 2, by = kLaneY[2] + kLaneH - 4;
         c.setColor(alpha(0xff3a4049, a));
         c.fill((x + 5) * s, by * s, (kCellW - 10) * s, 1.0f * s);
         if (l.velocity) {
            const float w = l.velocity / 64.0f * (kCellW / 2 - 5);
            c.setColor(alpha(l.velocity > 0 ? kOrange : kBlue, a));
            c.fill(std::min(mid, mid + w) * s, (by - 0.5f) * s, std::abs(w) * s, 2.0f * s);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      col_ = static_cast<int>(std::floor((x - kGridX) / kColumn));
      lane_ = -1;
      for (int l = 0; l < 3; ++l)
         if (y >= kLaneY[l] - 4 && y < kLaneY[l] + kLaneH + 4)
            lane_ = l;
      if (col_ < 0 || col_ >= kSteps || lane_ < 0)
         return;
      Pattern p = host_.pattern();
      Lane &l = p.lanes[col_];
      downY_ = y;
      if (e.repeatClickCount() >= 2) {
         if (lane_ == 0)
            l.octave = 0;
         else if (lane_ == 1)
            l.ratchet = 1;
         else
            l.velocity = 0;
         commit(p);
         lane_ = -1;
         return;
      }
      // P toggles.
      if (lane_ == 1 && x - (kGridX + col_ * kColumn) >= 23) {
         l.pressure = l.pressure ? 0 : 1;
         commit(p);
         lane_ = -1;
         return;
      }
      start_ = lane_ == 0 ? l.octave : lane_ == 1 ? l.ratchet : l.velocity;
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      if (lane_ < 0)
         return;
      const float s = ui::designScale(*this);
      const float dy = downY_ - e.position.y / s;
      Pattern p = host_.pattern();
      Lane &l = p.lanes[col_];
      if (lane_ == 0)
         l.octave = static_cast<int8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 14)), -3, 3));
      else if (lane_ == 1)
         l.ratchet = static_cast<uint8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 12)), 1, kMaxRatchet));
      else
         l.velocity = static_cast<int8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 2)), -64, 64));
      if (!(l == host_.pattern().lanes[col_]))
         commit(p);
   }

   void mouseUp(const visage::MouseEvent &) override { lane_ = -1; }

   bool mouseWheel(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      const int col = static_cast<int>(std::floor((x - kGridX) / kColumn));
      int lane = -1;
      for (int l = 0; l < 3; ++l)
         if (y >= kLaneY[l] - 4 && y < kLaneY[l] + kLaneH + 4)
            lane = l;
      if (col < 0 || col >= kSteps || lane < 0 || e.wheel_delta_y == 0.0f)
         return false;
      const int by = e.wheel_delta_y > 0 ? 1 : -1;
      Pattern p = host_.pattern();
      Lane &l = p.lanes[col];
      if (lane == 0)
         l.octave = static_cast<int8_t>(std::clamp(l.octave + by, -3, 3));
      else if (lane == 1)
         l.ratchet = static_cast<uint8_t>(std::clamp(l.ratchet + by, 1, kMaxRatchet));
      else
         l.velocity = static_cast<int8_t>(std::clamp(l.velocity + 4 * by, -64, 64));
      commit(p);
      return true;
   }

private:
   void commit(const Pattern &p) {
      if (onEdit)
         onEdit(p);
      redraw();
   }

   EditorHost &host_;
   int col_ = -1, lane_ = -1, start_ = 0;
   float downY_ = 0.0f;
};

// The settings card: Timing, Playback and Pressure, each under a grey heading and rule, with thin
// dividers between them.
class SettingsCard : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(t.panel);
      c.roundedRectangle(0, 0, width(), height(), 6 * s);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 6 * s, 1.0f);
      struct Group {
         const char *title, *note;
         float x, w;
      };
      const Group groups[] = {{"TIMING", nullptr, 15, 202}, {"PLAYBACK", nullptr, 258, 138}, {"PRESSURE", "from each finger", 438, 423}};
      for (const Group &g : groups) {
         c.setColor(t.dimText);
         c.text(g.title, ui::font(11 * s), visage::Font::kLeft, g.x * s, 8 * s, 120 * s, 20 * s);
         if (g.note) {
            c.setColor(alpha(t.dimText, 0.8f));
            c.text(g.note, ui::font(9.5f * s), visage::Font::kLeft, (g.x + 66) * s, 8 * s, 160 * s, 20 * s);
         }
         c.setColor(kRule);
         c.fill(g.x * s, 31 * s, g.w * s, 1.0f * s);
      }
      c.setColor(kRule);
      c.fill(237 * s, 12 * s, 1.0f * s, 116 * s);
      c.fill(417 * s, 12 * s, 1.0f * s, 116 * s);
   }
};

// The dice card's face: its title, what each button does, and the ROLL / RANGE list's words. The
// buttons and boxes are the panel's, laid over it.
class DiceCard : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(0xff1a1e24);
      c.roundedRectangle(0, 0, width(), height(), 8 * s);
      c.setColor(0xff3a414c);
      c.roundedRectangleBorder(0, 0, width(), height(), 8 * s, 1.2f * s);
      c.setColor(kOrange);
      c.text("DICE", ui::font(16 * s), visage::Font::kLeft, 16 * s, 12 * s, 100 * s, 24 * s);
      c.setColor(t.dimText);
      const auto lines = [&](const char *a, const char *b, float y) {
         c.text(a, ui::font(9 * s), visage::Font::kLeft, 176 * s, y * s, 176 * s, 13 * s);
         c.text(b, ui::font(9 * s), visage::Font::kLeft, 176 * s, (y + 13) * s, 176 * s, 13 * s);
      };
      lines("builds a new pattern from a plain", "base, each item within its range", 54);
      lines("nudges the pattern you have,", "each item by up to its range", 96);
      c.text("ROLL", ui::font(9.5f * s), visage::Font::kLeft, 16 * s, 140 * s, 80 * s, 16 * s);
      c.text("RANGE", ui::font(9.5f * s), visage::Font::kRight, 0, 140 * s, (width() / s - 16) * s, 16 * s);
      c.setColor(kRule);
      c.fill(16 * s, 158 * s, (width() / s - 32) * s, 1.0f * s);
      static const char *const kWhat[kDiceItemCount] = {"how many steps get a note", "how far up the held rows",
                                                        "note lengths, from 1 step", "away from 0", "hits, from 1",
                                                        "offset, away from 0", "steps that follow it (P)"};
      for (int i = 0; i < kDiceItemCount; ++i) {
         c.setColor(alpha(t.dimText, on[i] ? 1.0f : 0.5f));
         c.text(kWhat[i], ui::font(9 * s), visage::Font::kLeft, 112 * s, (166 + i * 29) * s, 140 * s, 24 * s);
      }
      c.setColor(t.dimText);
      c.text("Unticked items stay as they are. The plain base: one note per step", ui::font(8.5f * s),
             visage::Font::kLeft, 16 * s, (height() / s - 34) * s, (width() / s - 32) * s, 13 * s);
      c.text("on row 1, octave 0, 1 hit, velocity 0. Only held notes are ever used.", ui::font(8.5f * s),
             visage::Font::kLeft, 16 * s, (height() / s - 21) * s, (width() / s - 32) * s, 13 * s);
   }
   void mouseDown(const visage::MouseEvent &) override {}

   bool on[kDiceItemCount] = {};
};

const ui::Theme &switchTheme() {
   static const ui::Theme t = [] {
      ui::Theme theme = ui::defaultTheme();
      theme.background = theme.text; // the selected option's words, on slate
      return theme;
   }();
   return t;
}

// A button or tick that does nothing for now, or is off: its words dimmed.
const ui::Theme &offTheme() {
   static const ui::Theme t = [] {
      ui::Theme theme = switchTheme();
      theme.text = 0xff6b717a;
      return theme;
   }();
   return t;
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::UpdateNotice>({150, 19, 196, 26});
      tips_.add(add<ui::HoverArea>({14, 12, 130, 40}), "JArp version " JCLAP_PLUGIN_VERSION);
      presets_ = &add<ui::PresetBar>({546, 18, 336, 28}, factoryPresets(), kPresetFormat, host_);
      presets_->setAccent(kOrange);
      presets_->defaults = defaultValues();
      presets_->currentName = [this] { return host_.presetName(); };
      presets_->setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      presets_->onLoad = [this](const ui::Preset &p) {
         ui::applyPreset(p, host_, defaultValues());
         if (const Pattern *pattern = factoryPattern(p.name))
            edit(*pattern);
      };
      presets_->onDice = [this] { showDice(!host_.diceOpen); };
      presets_->diceLit = [this] { return host_.diceOpen; };
      tips_.add(*presets_, "Presets: the arrows step through them, the name opens the menu, the star marks a favorite and "
                           "SAVE keeps the current settings. A factory preset brings its pattern; a saved one keeps the "
                           "settings, and the pattern stays with the project. The die opens the dice.");

      // PATTERN: the grid and its lanes, with how rows are picked in its title row.
      add<ui::Section>({12, 76, 876, 478}, "PATTERN");
      rowsLabel(470, "ROWS BY");
      auto &rowsBy = choice(kRowsBy, {552, 89, 115, 22}, {"PITCH", "DEGREE"});
      tips_.add(rowsBy, "Pitch: row 1 is the lowest note held, row 2 the next up, and so on. Degree: rows count up the "
                        "scale from the lowest note held; a degree that isn't held plays the nearest note that is.");
      emptyLabel_ = &rowsLabel(680, "EMPTY ROWS");
      empty_ = &choice(kEmptyRows, {773, 89, 99, 22}, {"REST", "WRAP"});
      tips_.add(*empty_, "Rows above the notes held: Rest stays silent; Wrap starts again from the lowest note.");
      scaleLabel_ = &rowsLabel(650, "SCALE");
      key_ = &add<ui::Dropdown>({735, 89, 52, 22}, std::vector<std::string>(std::begin(kKeyNames), std::end(kKeyNames)));
      key_->setAccent(kSlate);
      bindings_.bind(*key_, kKey);
      scale_ = &add<ui::Dropdown>({793, 89, 79, 22}, std::vector<std::string>{"MAJOR", "MINOR"});
      scale_->setAccent(kSlate);
      bindings_.bind(*scale_, kScale);
      tips_.add(*key_, "The scale's key, for Degree rows.");
      tips_.add(*scale_, "The scale Degree rows count up. Where two held notes are equally near a degree, the one in "
                         "the scale wins.");

      grid_ = &add<GridView>({20, 116, 862, 324}, host_);
      grid_->onEdit = [this](const Pattern &p) { edit(p); };
      tips_.add(*grid_, "Click a cell for a note and drag right to lengthen it; drag a note's right edge to set how "
                        "long it sounds; click a note to remove it. Drag the purple bar to set the pattern's length.");
      add<ui::Divider>({27, 445, 846, 1});
      lanes_ = &add<LaneView>({20, 450, 862, 96}, host_);
      lanes_->onEdit = [this](const Pattern &p) { edit(p); };
      tips_.add(*lanes_, "Per step, for the notes starting on it: drag up or down, scroll, or double-click to reset. "
                         "Click P to make that step's ratchet follow pressure.");

      // Settings.
      add<SettingsCard>({12, 568, 876, 142});
      tips_.add(knob(kRate, "RATE", kOrange, 37, 606), "How long each step is.");
      tips_.add(knob(kSwing, "SWING", kOrange, 107, 606), "Moves every second step later: 50% is straight.");
      tips_.add(knob(kLength, "LENGTH", kOrange, 177, 606), "How many steps the pattern plays before it loops. The "
                                                            "purple bar on the grid is the same.");
      heading("LATCH", 270, 606);
      tips_.add(choice(kLatch, {270, 624, 80, 22}, {"OFF", "ON"}),
                "On: the notes keep playing after you let go, until you play a new chord.");
      heading("RESTART", 270, 652);
      tips_.add(choice(kRestart, {270, 670, 138, 22}, {"FREE", "NOTE", "BAR"}),
                "Where the pattern starts: Free follows the song position; Note starts it with the first note you "
                "play; Bar starts it at the bar the first note is in.");
      tips_.add(knob(kPressVelocity, "VELOCITY", kBlue, 452, 606),
                "How far each note's velocity follows its finger's pressure instead of how hard the key was struck. The "
                "Velocity lane's offset is added after.");
      tips_.add(knob(kCurve, "CURVE", kBlue, 560, 606, true),
                "How pressure maps: Soft reaches high values with a light touch, Hard needs a firm press.");
      tips_.add(knob(kPressRatchet, "RATCHET", kBlue, 670, 606),
                "How far pressure adds hits on steps with P lit, from the step's own count up to Max Hits.");
      tips_.add(knob(kMaxHits, "MAX HITS", kBlue, 780, 606), "The most hits a pressed P step splits into.");

      buildDice();
      showDice(host_.diceOpen);
      sync();
      startTimer(16);
   }

   void timerCallback() override { sync(); }

   void sync() {
      const bool degree = std::lround(host_.paramValue(kRowsBy)) != 0;
      emptyLabel_->setVisible(!degree);
      empty_->setVisible(!degree);
      scaleLabel_->setVisible(degree);
      key_->setVisible(degree);
      scale_->setVisible(degree);
      // Over the grid, the dice card hides what's under it.
      grid_->redraw();
      lanes_->redraw();
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      canvas.setColor(t.background);
      canvas.fill(0, 0, width(), height());
      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      // The logo: a ring with three dots stepping up, like notes in a row.
      canvas.setColor(kOrange);
      canvas.ring(19 * s, 17 * s, 30 * s, 2.0f * s);
      canvas.circle(25 * s, 34 * s, 5 * s);
      canvas.circle(31.5f * s, 28.5f * s, 5 * s);
      canvas.circle(38 * s, 23 * s, 5 * s);
      canvas.text("J", ui::font(28 * s), visage::Font::kLeft, 59 * s, 12 * s, 30 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("Arp", ui::font(28 * s), visage::Font::kLeft, 72 * s, 12 * s, 80 * s, 40 * s);

      // The legend for the rows' hairlines, and what they are.
      const float y = 725;
      const struct {
         unsigned colour;
         const char *name;
         float x;
      } keys[] = {{kBlue, "Pressure", 14}, {kPurple, "Slide (CC74)", 88}, {kOrange, "Bend", 177}};
      for (const auto &k : keys) {
         canvas.setColor(k.colour);
         canvas.fill(k.x * s, y * s, 12 * s, 1.5f * s);
         canvas.setColor(t.dimText);
         canvas.text(k.name, ui::font(9.5f * s), visage::Font::kLeft, (k.x + 18) * s, (y - 7) * s, 120 * s, 14 * s);
      }
      canvas.text("Each row's finger, live. MPE passes through on its own channel.", ui::font(9.5f * s),
                  visage::Font::kRight, 400 * s, (y - 7) * s, 486 * s, 14 * s);
   }

   void showDice(bool open) {
      host_.diceOpen = open;
      for (visage::Frame *f : dice_)
         f->setVisible(open);
      refreshDice();
      if (presets_)
         presets_->redraw();
   }

private:
   void edit(const Pattern &p) {
      host_.setPattern(p);
      grid_->redraw();
      lanes_->redraw();
   }

   ui::Label &rowsLabel(float x, const char *name) {
      return add<ui::Label>({x, 89, 80, 22}, name, ui::defaultTheme().dimText, visage::Font::kRight, 0.5f);
   }

   void heading(const char *name, float x, float y) {
      add<ui::Label>({x, y, 120, 14}, name, ui::defaultTheme().dimText, visage::Font::kLeft, 0.8f);
   }

   ui::SegmentedSwitch &choice(clap_id id, ui::Rect r, std::vector<std::string> options) {
      auto &sw = add<ui::SegmentedSwitch>(r, std::move(options));
      sw.setTheme(switchTheme());
      sw.setAccent(kSlate);
      bindings_.bind(sw, id);
      return sw;
   }

   ui::Knob &knob(clap_id id, const char *label, unsigned accent, float x, float y, bool bipolar = false) {
      const auto &p = kParams[id];
      const bool stepped = (p.flags & CLAP_PARAM_IS_STEPPED) != 0;
      auto &k = add<ui::Knob>({x, y, 76, 88}, ui::ValueRange{p.min, p.max, p.defaultValue, stepped ? 1.0 : 0.0, bipolar, stepped});
      k.setLabel(label);
      k.setAccent(accent);
      k.setLabelColor(ui::defaultTheme().dimText);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      return k;
   }

   // ---- dice ----

   static constexpr ui::Rect kDiceBox{523, 71, 358, 408};

   void buildDice() {
      const ui::Rect b = kDiceBox;
      card_ = &add<DiceCard>(b);
      dice_.push_back(card_);
      const auto keep = [this](visage::Frame &f) { dice_.push_back(&f); };
      auto &rnd = add<ui::Button>({b.x + 16, b.y + 50, 150, 33}, "RANDOMIZE");
      rnd.setAccent(kOrange);
      rnd.onClick = [this] { roll(false); };
      keep(rnd);
      tips_.add(rnd, "A new pattern from a plain base, each ticked item rolled within its range.");
      auto &mut = add<ui::Button>({b.x + 16, b.y + 92, 150, 33}, "MUTATE");
      mut.setAccent(kOrange);
      mut.onClick = [this] { roll(true); };
      keep(mut);
      tips_.add(mut, "Nudges the pattern you have, each ticked item by up to its range. Click again to keep going.");
      undo_ = &add<ui::Button>({b.x + b.w - 136, b.y + 14, 58, 22}, "UNDO");
      undo_->onClick = [this] { stepHistory(false); };
      keep(*undo_);
      redo_ = &add<ui::Button>({b.x + b.w - 72, b.y + 14, 56, 22}, "REDO");
      redo_->onClick = [this] { stepHistory(true); };
      keep(*redo_);

      DiceSettings &d = host_.dice;
      for (int i = 0; i < kDiceItemCount; ++i) {
         const float y = b.y + 166 + i * 29;
         auto &tick = add<ui::Button>({b.x + 16, y, 90, 24}, kDiceItemNames[i]);
         tick.setToggle(true);
         tick.setAccent(kSlate);
         tick.setOn(d.on[i]);
         tick.setTheme(d.on[i] ? switchTheme() : offTheme());
         card_->on[i] = d.on[i];
         tick.onToggle = [this, i, &tick](bool on) {
            tick.setTheme(on ? switchTheme() : offTheme());
            host_.dice.on[i] = on;
            card_->on[i] = on;
            card_->redraw();
         };
         keep(tick);
         tips_.add(tick, "Ticked, the dice roll this; unticked, it stays as it is.");
      }
      const auto range = [&](int i, ui::ValueRange r, double value, std::function<std::string(double)> show,
                             std::function<void(double)> set) {
         auto &box = add<ui::NumberBox>({b.x + b.w - 90, b.y + 166 + i * 29, 74, 24}, r);
         box.setAccent(kOrange);
         box.valueText = show;
         box.setValue(value);
         box.onChange = set;
         keep(box);
      };
      range(kDicePlacement, {0, 1, 0.25, 0.05}, d.placement,
            [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + "%"; },
            [this](double v) { host_.dice.placement = v; });
      range(kDiceNotes, {1, 8, 3, 1, false, true}, d.rows,
            [](double v) { const int n = static_cast<int>(std::lround(v)); return n == 1 ? std::string("row 1") : "rows 1-" + std::to_string(n); },
            [this](double v) { host_.dice.rows = static_cast<int>(std::lround(v)); });
      range(kDiceLength, {1, 16, 8, 1, false, true}, d.length,
            [](double v) {
               const int q = static_cast<int>(std::lround(v));
               static const char *const kQ[] = {"", "\xc2\xbc", "\xc2\xbd", "\xc2\xbe"};
               const std::string most = q % 4 == 0 ? std::to_string(q / 4) : (q >= 4 ? std::to_string(q / 4) : "") + kQ[q % 4];
               return q == 1 ? std::string("\xc2\xbc only") : "\xc2\xbc to " + most;
            },
            [this](double v) { host_.dice.length = static_cast<int>(std::lround(v)); });
      range(kDiceOctave, {0, 3, 1, 1, false, true}, d.octave,
            [](double v) { const int n = static_cast<int>(std::lround(v)); return n ? "\xc2\xb1" + std::to_string(n) + " oct" : std::string("none"); },
            [this](double v) { host_.dice.octave = static_cast<int>(std::lround(v)); });
      range(kDiceRatchet, {1, 8, 3, 1, false, true}, d.ratchet,
            [](double v) { return "up to " + std::to_string(static_cast<int>(std::lround(v))); },
            [this](double v) { host_.dice.ratchet = static_cast<int>(std::lround(v)); });
      range(kDiceVelocity, {0, 64, 20, 1, false, true}, d.velocity,
            [](double v) { return "\xc2\xb1" + std::to_string(static_cast<int>(std::lround(v))); },
            [this](double v) { host_.dice.velocity = static_cast<int>(std::lround(v)); });
      range(kDicePressure, {0, 1, 0.2, 0.05}, d.pressure,
            [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + "%"; },
            [this](double v) { host_.dice.pressure = v; });
   }

   void roll(bool mutate) {
      const Pattern now = host_.pattern();
      const uint64_t seed = DiceRng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                    ++diceSeed_)
                               .next();
      const int len = static_cast<int>(std::lround(host_.paramValue(kLength)));
      const Pattern next = mutate ? mutatePattern(now, host_.dice, len, seed) : randomizePattern(now, host_.dice, len, seed);
      host_.diceHistory.push(now);
      edit(next);
      refreshDice();
   }

   void stepHistory(bool forward) {
      DiceHistory &h = host_.diceHistory;
      if (forward ? !h.canRedo() : !h.canUndo())
         return;
      edit(forward ? h.redo(host_.pattern()) : h.undo(host_.pattern()));
      refreshDice();
   }

   void refreshDice() {
      if (undo_)
         undo_->setTheme(host_.diceHistory.canUndo() ? switchTheme() : offTheme());
      if (redo_)
         redo_->setTheme(host_.diceHistory.canRedo() ? switchTheme() : offTheme());
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::PresetBar *presets_ = nullptr;
   GridView *grid_ = nullptr;
   LaneView *lanes_ = nullptr;
   ui::Label *emptyLabel_ = nullptr, *scaleLabel_ = nullptr;
   ui::SegmentedSwitch *empty_ = nullptr;
   ui::Dropdown *key_ = nullptr, *scale_ = nullptr;
   DiceCard *card_ = nullptr;
   ui::Button *undo_ = nullptr, *redo_ = nullptr;
   std::vector<visage::Frame *> dice_;
   uint64_t diceSeed_ = 0;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, bool dice) {
   Panel panel(host);
   panel.stopTimer();
   panel.showDice(dice);
   panel.setBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
   panel.sync();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jarp
