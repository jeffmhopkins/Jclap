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
#include <filesystem>
#include <functional>
#include <memory>
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

// On a ratcheted note, whose hits are drawn as orange bars, a dark pill behind a tag so it reads.
void tagBacking(visage::Canvas &c, float s, float x, float y, float w, float a, bool ratcheted) {
   if (!ratcheted)
      return;
   c.setColor(alpha(0xff3b2a1a, a));
   c.roundedRectangle(x * s, y * s, w * s, 9 * s, 2 * s);
}

// A note's width for a length in quarter steps: a whole step is a cell, and longer ones span the
// gaps between cells.
float noteWidth(int quarters) {
   return quarters * kColumn / kQuarters - (kColumn - kCellW) * std::min(1.0f, quarters / static_cast<float>(kQuarters));
}

const char *ordinal(int row) {
   static const char *const kNames[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
   return kNames[std::clamp(row, 0, 7)];
}

// Which steps the grid and the lanes show: kView of them from `first`. A pattern longer than kView
// scrolls, following the playhead unless the scroll bar was moved by hand: the view holds still until
// the playhead reaches its middle, then moves a step at a time to keep it there, and stops once the
// pattern's last step is at the right edge.
struct GridScroll {
   int first = 0;
   bool follow = true;
   // The notes picked (right-click, shift-click or a box) for their own lane values, by slot and
   // each one's row and step; slot -1 for none.
   int pickSlot = -1;
   std::vector<std::pair<int, int>> picks;
   // The slot the grid and lanes show and edit: the selected one, or the one playing once CHORD or
   // LOOP moves on to it. Set by the panel's sync.
   int view = 0;
   int lastSelected = -1, lastPlaying = -1;

   // Picks `selected` when it changes, the playing slot when that changes, and the selected one again
   // when playback stops.
   void track(int selected, int playing) {
      if (selected != lastSelected || playing < 0)
         view = selected;
      else if (playing != lastPlaying)
         view = playing;
      lastSelected = selected;
      lastPlaying = playing;
   }

   // The picked notes' indices in `p` (the pattern in `slot`).
   std::vector<int> picked(const Pattern &p, int slot) const {
      std::vector<int> out;
      if (pickSlot == slot)
         for (const auto &[row, step] : picks)
            if (const int i = p.noteStarting(row, step); i >= 0)
               out.push_back(i);
      return out;
   }
   bool isPicked(const Pattern &p, int slot, int i) const {
      const auto v = picked(p, slot);
      return std::find(v.begin(), v.end(), i) != v.end();
   }
   // The first picked note starting on `step`, or -1.
   int pickedOn(const Pattern &p, int slot, int step) const {
      for (int i : picked(p, slot))
         if (p.notes[i].step == step)
            return i;
      return -1;
   }
   bool any(const Pattern &p, int slot) const { return !picked(p, slot).empty(); }
   // Picks `n` alone, or adds it to (or takes it from) the picked notes.
   void pick(int slot, const Note &n) {
      pickSlot = slot;
      picks = {{n.row, n.step}};
   }
   void toggle(int slot, const Note &n) {
      if (pickSlot != slot)
         picks.clear();
      pickSlot = slot;
      const std::pair<int, int> k{n.row, n.step};
      const auto it = std::find(picks.begin(), picks.end(), k);
      if (it != picks.end())
         picks.erase(it);
      else
         picks.push_back(k);
   }
   void pickAll(int slot, const Pattern &p, const std::vector<int> &which) {
      pickSlot = slot;
      picks.clear();
      for (int i : which)
         picks.push_back({p.notes[i].row, p.notes[i].step});
   }
   void unpick() {
      pickSlot = -1;
      picks.clear();
   }

   // Brings `first` up to date for a pattern of `len` steps with the playhead on `playStep` (or -1).
   void update(int len, int playStep) {
      if (len <= kView) {
         first = 0;
         return;
      }
      if (follow && playStep >= 0)
         first = playStep - kView / 2;
      first = std::clamp(first, 0, len - kView);
   }
};

// The grid of notes, with the row labels at its left, the step numbers under it and the pattern's
// end bar.
class GridView : public visage::Frame {
public:
   GridView(EditorHost &host, GridScroll &scroll) : host_(host), scroll_(scroll) {}
   // The pattern on show.
   const Pattern &shown() const { return host_.bank().slots[static_cast<size_t>(scroll_.view)]; }

   std::function<void(const Pattern &)> onEdit;

   // The bottom of the step numbers, where the scroll bar runs.
   static constexpr float kNumbersY = kGridY + 7 * kRowPitch + kCellH + 6;
   static constexpr float kBarY = kNumbersY + 17, kBarW = kView * kColumn - (kColumn - kCellW);

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Pattern &p = shown();
      const jarp::Frame &f = host_.frame();
      const int len = length();
      const int rowsBy = static_cast<int>(std::lround(host_.paramValue(kRowsBy)));
      const bool degree = rowsBy == 1 || rowsBy == 2;
      // The playhead shows on the pattern playing only (another may be selected for editing).
      const bool here = f.running && f.slot == scroll_.view;
      const int playStep = here ? std::clamp(static_cast<int>(f.position), 0, len - 1) : -1;
      const int playQuarter = here ? static_cast<int>(f.position * kQuarters) : -1;
      const int first = scroll_.first;

      // The playhead's column, behind the cells.
      if (playStep >= first && playStep < first + kView) {
         const float x = (kGridX + (playStep - first) * kColumn - 3) * s;
         c.setColor(0xff4a515c);
         c.roundedRectangleBorder(x, (kGridY - 8) * s, (kCellW + 6) * s, (7 * kRowPitch + kCellH + 16) * s, 5 * s, 1.2f * s);
      }

      for (int r = 0; r < kRows; ++r) {
         const float y = rowY(r);
         for (int col = 0; col < kView; ++col) {
            c.setColor(first + col < len ? kCell : kCellOff);
            c.roundedRectangle((kGridX + col * kColumn) * s, y * s, kCellW * s, kCellH * s, 3 * s);
         }
      }

      // Notes, clipped to the steps in view (a long note may start before them). Each shows its own
      // lane over its step's: softer notes fainter, the octave as a tag, ratchets as splits, a dot
      // when it has values of its own, and a white edge on the notes picked for editing.
      const std::vector<int> picked = scroll_.picked(p, scroll_.view);
      c.saveState();
      c.trimClampBounds((kGridX - 1) * s, 0, (kBarW + 2) * s, height());
      bool anyHeld = false;
      for (int r = 0; r < kRows; ++r)
         anyHeld = anyHeld || f.rows[r].key >= 0;
      for (int i = 0; i < p.count; ++i) {
         const Note &n = p.notes[i];
         if (n.step >= first + kView || n.step * kQuarters + n.length <= first * kQuarters)
            continue;
         const Lane lane = p.laneOf(i);
         const bool live = n.step < len;
         // A note on a row no held note plays (too few held, or a degree not held) greys out.
         const bool muted = anyHeld && f.rows[n.row].key < 0;
         const unsigned tone = muted ? 0xff5a606b : kOrange;
         const float x = kGridX + (n.step - first) * kColumn, y = rowY(n.row);
         const int quarters = std::min<int>(n.length, live ? (len - n.step) * kQuarters : n.length);
         const float w = noteWidth(quarters);
         const bool sounding = live && f.running && f.rows[n.row].key >= 0 && playQuarter >= n.step * kQuarters &&
                               playQuarter < n.step * kQuarters + quarters;
         const float a = (live ? 1.0f : 0.3f) * (1.0f - 0.45f * std::max(0, -lane.velocity) / 64.0f);
         const int base = lane.ratchet;
         const int extra = lane.pressure ? std::max(0, maxHits() - base) : 0;
         if (base <= 1 && !extra) {
            c.setColor(alpha(sounding ? (muted ? 0xff6a707b : 0xffffa94d) : tone, a));
            c.roundedRectangle(x * s, y * s, w * s, kCellH * s, 3 * s);
         }
         else {
            // Ratchets: the note splits into its hits, the extra slots pressure can fill dashed in
            // blue and filled as it does.
            c.setColor(alpha(0xff3b2a1a, a));
            c.roundedRectangle(x * s, y * s, w * s, kCellH * s, 3 * s);
            c.setColor(alpha(sounding ? 0xffffffff : tone, a));
            c.roundedRectangleBorder(x * s, y * s, w * s, kCellH * s, 3 * s, 1.2f * s);
            const int slots = base + extra;
            const int lit = lane.pressure && f.hits[n.step] ? std::clamp<int>(f.hits[n.step] - base, 0, extra) : 0;
            const float pad = 3.0f, gap = 2.0f;
            const float each = (w - 2 * pad - gap * (slots - 1)) / slots;
            for (int h = 0; h < slots; ++h) {
               const float hx = x + pad + h * (each + gap);
               if (h < base) {
                  c.setColor(alpha(tone, a));
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
         // The length handle, lit under the mouse and while dragging it.
         if (i == hoverEdge_ || (drag_ == Drag::Length && note_ == i && moved_)) {
            c.setColor(0xffffffff);
            c.roundedRectangle((x + w - 3) * s, (y + 3) * s, 2 * s, (kCellH - 6) * s, 1 * s);
         }
         if (lane.octave) {
            char o[8];
            std::snprintf(o, sizeof(o), "%+d", lane.octave);
            tagBacking(c, s, x + w - 17, y + 2, 14, a, base > 1 || extra);
            c.setColor(alpha(base <= 1 && !extra ? 0xff3b2a1a : tone, a));
            c.text(o, ui::font(8.5f * s), visage::Font::kRight, x * s, (y + 1) * s, (w - 4) * s, 11 * s);
         }
         // Chance under 100: the percent in the note's lower corner.
         if (lane.chance < 100 && w >= 18) {
            char pc[8];
            std::snprintf(pc, sizeof(pc), "%d%%", lane.chance);
            tagBacking(c, s, x + w - (lane.chance < 10 ? 16 : 21), y + kCellH - 11, lane.chance < 10 ? 14 : 19, a,
                       base > 1 || extra);
            c.setColor(alpha(base <= 1 && !extra ? 0xff3b2a1a : tone, a));
            c.text(pc, ui::font(8 * s), visage::Font::kRight, x * s, (y + kCellH - 12) * s, (w - 3) * s, 11 * s);
         }
         if (n.own) {
            c.setColor(alpha(0xffffffff, live ? 0.9f : 0.4f));
            c.circle((x + 3) * s, (y + 3) * s, 4 * s);
         }
         if (std::find(picked.begin(), picked.end(), i) != picked.end()) {
            c.setColor(0xffffffff);
            c.roundedRectangleBorder((x - 2) * s, (y - 2) * s, (w + 4) * s, (kCellH + 4) * s, 4 * s, 1.5f * s);
         }
      }
      c.restoreState();

      // The box being dragged out to pick notes.
      if (drag_ == Drag::Box) {
         const float bx = std::min(boxX0_, boxX1_), by = std::min(boxY0_, boxY1_);
         const float bw = std::abs(boxX1_ - boxX0_), bh = std::abs(boxY1_ - boxY0_);
         c.setColor(alpha(0xffffffff, 0.08f));
         c.fill(bx * s, by * s, bw * s, bh * s);
         c.setColor(alpha(0xffffffff, 0.7f));
         c.roundedRectangleBorder(bx * s, by * s, bw * s, bh * s, 2 * s, 1.0f * s);
      }

      // The playhead line, moving through the step in time with the transport.
      if (here && f.position >= first && f.position < first + kView) {
         const float px = kGridX + static_cast<float>(f.position - first) * kColumn;
         c.setColor(alpha(0xffffffff, 0.85f));
         c.fill((px - 0.75f) * s, (kGridY - 8) * s, 1.5f * s, (7 * kRowPitch + kCellH + 16) * s);
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
      for (int col = 0; col < kView; ++col) {
         const int step = first + col;
         c.setColor(step >= len ? alpha(t.dimText, 0.45f) : step % 4 == 0 ? t.text : t.dimText);
         c.text(std::to_string(step + 1), ui::font(9.5f * s), visage::Font::kCenter, (kGridX + col * kColumn) * s,
                kNumbersY * s, kCellW * s, 16 * s);
      }

      // A pattern longer than the grid: a scroll bar under the step numbers, with the steps that
      // start notes ticked along it and the playhead in orange, and the steps in view and FOLLOW at
      // its left.
      if (len > kView) {
         c.setColor(0xff22262d);
         c.roundedRectangle(kGridX * s, kBarY * s, kBarW * s, 5 * s, 2.5f * s);
         c.setColor(0xff4a515c);
         for (int i = 0; i < p.count; ++i)
            if (p.notes[i].step < len)
               c.fill((kGridX + p.notes[i].step * kBarW / len) * s, (kBarY + 1) * s, std::max(1.0f, kBarW / len) * s, 3 * s);
         c.setColor(alpha(t.text, hoverBar_ || drag_ == Drag::Scroll ? 0.45f : 0.3f));
         c.roundedRectangle((kGridX + first * kBarW / len) * s, (kBarY - 1) * s, (kView * kBarW / len) * s, 7 * s, 3 * s);
         if (playStep >= 0) {
            c.setColor(kOrange);
            c.fill((kGridX + (f.position * kBarW / len) - 1) * s, (kBarY - 2) * s, 2 * s, 9 * s);
         }
         char range[32];
         std::snprintf(range, sizeof(range), "%d-%d of %d", first + 1, first + kView, len);
         c.setColor(t.dimText);
         c.text(range, ui::font(9 * s), visage::Font::kLeft, 7 * s, kNumbersY * s, 80 * s, 16 * s);
         const float fx = kFollowX, fy = kNumbersY + 1;
         c.setColor(scroll_.follow ? kSlate : t.button);
         c.roundedRectangle(fx * s, fy * s, kFollowW * s, 14 * s, 3 * s);
         c.setColor(t.panelBorder);
         c.roundedRectangleBorder(fx * s, fy * s, kFollowW * s, 14 * s, 3 * s, 1.0f);
         c.setColor(scroll_.follow ? kOrange : t.dimText);
         c.text("FOLLOW", ui::font(8 * s), visage::Font::kCenter, fx * s, fy * s, kFollowW * s, 14 * s);
      }

      // The pattern's end: a purple bar with a tab to drag.
      if (len < first || len > first + kView)
         return;
      const float bx = barX(len - first);
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
      const Pattern &p = shown();
      const int first = scroll_.first;
      // The scroll bar and FOLLOW, for a pattern longer than the grid.
      if (length() > kView && y >= kNumbersY) {
         if (x >= kFollowX && x < kFollowX + kFollowW) {
            scroll_.follow = !scroll_.follow;
            redrawAll();
            return;
         }
         if (x >= kGridX - 4) {
            drag_ = Drag::Scroll;
            scrollTo(x);
         }
         return;
      }
      // The end bar, anywhere along it.
      if (length() >= first && length() <= first + kView && std::abs(x - barX(length() - first)) < 7 &&
          y < kGridY + 8 * kRowPitch) {
         drag_ = Drag::End;
         return;
      }
      const int col = static_cast<int>(std::floor((x - kGridX) / kColumn));
      const int row = 7 - static_cast<int>(std::floor((y - kGridY) / kRowPitch));
      if (col < 0 || col >= kView || row < 0 || row >= kRows || y < kGridY)
         return;
      const int quarter = first * kQuarters + static_cast<int>(std::floor((x - kGridX) / kColumn * kQuarters));
      const int at = p.noteAt(row, std::min(quarter, kMaxLength - 1));
      const int slot = scroll_.view;
      // Right-click picks a note (or lets it go) for editing its own lane values below; shift-click
      // (either button) adds it to the picked notes or takes it out. From an empty cell, either drags
      // out a box that picks the notes it touches.
      const bool adding = e.isShiftDown();
      if (e.shouldTriggerPopup() || adding) {
         if (at >= 0) {
            const std::vector<int> now = scroll_.picked(p, slot);
            if (adding)
               scroll_.toggle(slot, p.notes[at]);
            else if (now.size() == 1 && now[0] == at)
               scroll_.unpick();
            else
               scroll_.pick(slot, p.notes[at]);
            redrawAll();
            return;
         }
         if (!adding)
            scroll_.unpick();
         else if (scroll_.pickSlot != slot)
            scroll_.unpick();
         boxBase_ = scroll_.picks;
         boxX0_ = boxX1_ = x;
         boxY0_ = boxY1_ = y;
         drag_ = Drag::Box;
         redrawAll();
         return;
      }
      Pattern next = p;
      if (at >= 0) {
         const Note &n = p.notes[at];
         // The lit right edge sets the length, once dragged a little.
         if (edgeAt(x, y) == at) {
            drag_ = Drag::Length;
            note_ = at;
            pressX_ = x;
            moved_ = false;
            return;
         }
         // Dragging a note moves it (and the other picked notes with it, if it's picked); a click
         // without a drag removes it, on the way up.
         drag_ = Drag::Move;
         note_ = at;
         moveBase_ = p;
         moveX_ = x;
         moveY_ = y;
         moved_ = false;
         moving_ = scroll_.isPicked(p, slot, at) ? scroll_.picked(p, slot) : std::vector<int>{at};
         movingKeys_.clear();
         for (int i : moving_)
            movingKeys_.push_back({p.notes[i].row, p.notes[i].step});
         basePicks_ = scroll_.picks;
         return;
      }
      // A cell's gap belongs to the cell before it.
      if (scroll_.pickSlot >= 0) {
         scroll_.unpick();
         redrawAll();
      }
      if (!next.add(row, first + col, kQuarters))
         return;
      commit(next);
      // A new note's length follows a drag to the right, but only a deliberate one: a click that
      // wobbles stays a one-step note.
      drag_ = Drag::Length;
      note_ = next.count - 1;
      pressX_ = x;
      moved_ = false;
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      if (drag_ == Drag::Scroll) {
         scrollTo(x);
         return;
      }
      if (drag_ == Drag::Box) {
         boxX1_ = x;
         boxY1_ = y;
         pickBox();
         redrawAll();
         return;
      }
      if (drag_ == Drag::Move) {
         if (!moved_ && std::abs(x - moveX_) < 5 && std::abs(y - moveY_) < 5)
            return;
         moved_ = true;
         const int steps = static_cast<int>(std::lround((x - moveX_) / kColumn));
         const int rows = -static_cast<int>(std::lround((y - moveY_) / kRowPitch));
         Pattern next = moveBase_;
         if (!next.move(moving_, rows, steps))
            return;
         // The picked notes that moved stay picked where they land.
         scroll_.picks = basePicks_;
         for (auto &k : scroll_.picks)
            if (std::find(movingKeys_.begin(), movingKeys_.end(), k) != movingKeys_.end())
               k = {k.first + rows, k.second + steps};
         if (next != shown())
            commit(next);
         redrawAll();
         return;
      }
      if (drag_ == Drag::End) {
         // Past the right edge the grid scrolls on, so the end can be dragged out to kSteps.
         const int len = std::clamp(scroll_.first + static_cast<int>(std::lround((x - kGridX + 2.5f) / kColumn)), 1, kSteps);
         if (len > scroll_.first + kView || len < scroll_.first) {
            scroll_.follow = false;
            scroll_.first = std::clamp(len > scroll_.first ? len - kView : len - 1, 0, kSteps - kView);
         }
         if (len != length()) {
            Pattern next = shown();
            next.length = static_cast<uint8_t>(len);
            commit(next);
         }
         return;
      }
      if (drag_ != Drag::Length)
         return;
      if (!moved_ && std::abs(x - pressX_) < kDragSlop)
         return;
      moved_ = true;
      Pattern next = shown();
      if (note_ < 0 || note_ >= next.count)
         return;
      Note &n = next.notes[note_];
      const float start = kGridX + (n.step - scroll_.first) * kColumn;
      const int quarters = static_cast<int>(std::lround((x - start + 2.5f) / kColumn * kQuarters));
      n.length = static_cast<uint16_t>(std::clamp(quarters, 1, next.room(n.row, n.step, note_)));
      commit(next);
   }

   void mouseUp(const visage::MouseEvent &) override {
      if (drag_ == Drag::Move && !moved_ && note_ >= 0 && note_ < shown().count) {
         Pattern next = shown();
         const std::pair<int, int> k{next.notes[note_].row, next.notes[note_].step};
         scroll_.picks.erase(std::remove(scroll_.picks.begin(), scroll_.picks.end(), k), scroll_.picks.end());
         next.remove(note_);
         commit(next);
      }
      const bool box = drag_ == Drag::Box;
      drag_ = Drag::None;
      note_ = -1;
      if (box || moved_)
         redrawAll();
      moved_ = false;
   }

   void mouseMove(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const bool over = length() > kView && e.position.y / s >= kNumbersY;
      const int edge = edgeAt(e.position.x / s, e.position.y / s);
      if (over != hoverBar_ || edge != hoverEdge_) {
         hoverBar_ = over;
         hoverEdge_ = edge;
         setCursorStyle(edge >= 0 ? visage::MouseCursor::HorizontalResize : visage::MouseCursor::Arrow);
         redraw();
      }
   }

   void mouseExit(const visage::MouseEvent &) override {
      if (hoverEdge_ >= 0 || hoverBar_) {
         hoverEdge_ = -1;
         hoverBar_ = false;
         setCursorStyle(visage::MouseCursor::Arrow);
         redraw();
      }
   }

   // The note whose right edge (its length handle) is under (x, y), or -1. The handle is a narrow
   // strip inside the edge; a short note keeps most of itself for moving.
   int edgeAt(float x, float y) const {
      const Pattern &p = shown();
      const int first = scroll_.first;
      const int row = 7 - static_cast<int>(std::floor((y - kGridY) / kRowPitch));
      if (row < 0 || row >= kRows || y < kGridY)
         return -1;
      for (int i = 0; i < p.count; ++i) {
         const Note &n = p.notes[i];
         if (n.row != row || n.step >= first + kView)
            continue;
         const float right = kGridX + (n.step - first) * kColumn + noteWidth(n.length);
         const float zone = std::min(kEdgeW, noteWidth(n.length) / 4);
         if (x <= right + 1 && x >= right - zone && right <= kGridX + kBarW + 1)
            return i;
      }
      return -1;
   }

   // The wheel, sideways or up and down, scrolls a long pattern.
   bool mouseWheel(const visage::MouseEvent &e) override {
      const int len = length();
      const float d = e.wheel_delta_x != 0.0f ? -e.wheel_delta_x : -e.wheel_delta_y;
      if (len <= kView || d == 0.0f)
         return false;
      scroll_.follow = false;
      scroll_.first = std::clamp(scroll_.first + (d > 0 ? 2 : -2), 0, len - kView);
      redrawAll();
      return true;
   }

   // Asked to redraw the lanes too when the view moves.
   std::function<void()> onScroll;

private:
   enum class Drag { None, Length, End, Scroll, Move, Box };

   // Picks the notes the box touches, with those picked before it when shift started it.
   void pickBox() {
      const Pattern &p = shown();
      const int slot = scroll_.view;
      const float x0 = std::min(boxX0_, boxX1_), x1 = std::max(boxX0_, boxX1_);
      const float y0 = std::min(boxY0_, boxY1_), y1 = std::max(boxY0_, boxY1_);
      scroll_.pickSlot = slot;
      scroll_.picks = boxBase_;
      for (int i = 0; i < p.count; ++i) {
         const Note &n = p.notes[i];
         const float nx = kGridX + (n.step - scroll_.first) * kColumn, ny = rowY(n.row);
         const float nx1 = std::min(nx + noteWidth(n.length), kGridX + kBarW);
         if (nx1 < x0 || nx > x1 || ny + kCellH < y0 || ny > y1 || nx1 < kGridX)
            continue;
         const std::pair<int, int> k{n.row, n.step};
         if (std::find(scroll_.picks.begin(), scroll_.picks.end(), k) == scroll_.picks.end())
            scroll_.picks.push_back(k);
      }
      if (scroll_.picks.empty())
         scroll_.pickSlot = -1;
   }

   static constexpr float kFollowX = 84.0f, kFollowW = 42.0f;

   void scrollTo(float x) {
      const int len = length();
      scroll_.follow = false;
      scroll_.first = std::clamp(static_cast<int>(std::lround((x - kGridX) / kBarW * len - kView / 2.0)), 0, len - kView);
      redrawAll();
   }
   void redrawAll() {
      redraw();
      if (onScroll)
         onScroll();
   }

   int length() const { return std::clamp<int>(shown().length, 1, kSteps); }
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
   GridScroll &scroll_;
   Drag drag_ = Drag::None;
   int note_ = -1;
   bool hoverBar_ = false;
   // The note whose length handle the mouse is over, lit; where a length drag started.
   int hoverEdge_ = -1;
   float pressX_ = 0.0f;
   static constexpr float kEdgeW = 5.0f, kDragSlop = 6.0f;
   // A move: the pattern before it, where it started, the notes moving and the picks before it.
   Pattern moveBase_;
   float moveX_ = 0.0f, moveY_ = 0.0f;
   bool moved_ = false;
   std::vector<int> moving_;
   std::vector<std::pair<int, int>> movingKeys_, basePicks_;
   // A box: its corners, and the picks it adds to.
   float boxX0_ = 0.0f, boxY0_ = 0.0f, boxX1_ = 0.0f, boxY1_ = 0.0f;
   std::vector<std::pair<int, int>> boxBase_;
};

// The lanes under the grid, a cell per step: Octave, Ratchet (with its P toggle) and Velocity.
// Drag a cell up or down (or scroll on it) to change it; double-click sets it back. With a note
// picked in the grid, its step's column edits that note's own values instead (outlined in white;
// a value it takes from the step is dim), and double-click hands a value back to the step.
class LaneView : public visage::Frame {
public:
   LaneView(EditorHost &host, const GridScroll &scroll) : host_(host), scroll_(scroll) {}
   // The pattern on show.
   const Pattern &shown() const { return host_.bank().slots[static_cast<size_t>(scroll_.view)]; }

   std::function<void(const Pattern &)> onEdit;

   static constexpr int kLanes = 4;
   static constexpr float kLaneY[kLanes] = {4.0f, 27.0f, 50.0f, 73.0f};
   static constexpr float kLaneH = 18.0f;

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Pattern &p = shown();
      const int len = std::clamp<int>(p.length, 1, kSteps);
      const char *const names[] = {"OCTAVE", "RATCHET", "VELOCITY", "CHANCE"};
      const int slot = scroll_.view;
      const size_t picks = scroll_.picked(p, slot).size();
      for (int lane = 0; lane < kLanes; ++lane) {
         c.setColor(t.dimText);
         c.text(names[lane], ui::font(10.5f * s), visage::Font::kLeft, 7 * s, kLaneY[lane] * s, 120 * s, kLaneH * s);
      }
      if (picks) {
         char label[16];
         if (picks == 1)
            std::snprintf(label, sizeof(label), "NOTE");
         else
            std::snprintf(label, sizeof(label), "%zu NOTES", picks);
         c.setColor(0xffffffff);
         c.text(label, ui::font(8 * s), visage::Font::kLeft, 74 * s, kLaneY[0] * s, 50 * s, kLaneH * s);
      }
      for (int col = 0; col < kView; ++col) {
         const int step = scroll_.first + col;
         const int picked = scroll_.pickedOn(p, slot, step);
         const bool mine = picked >= 0;
         const Lane l = mine ? p.laneOf(picked) : p.lanes[step];
         const uint8_t own = mine ? p.notes[picked].own : kOwnAll;
         const float base = step < len ? 1.0f : 0.35f;
         // A picked note's value taken from the step shows dim.
         auto dim = [&](uint8_t bit) { return own & bit ? base : 0.45f * base; };
         float a = base;
         const float x = kGridX + col * kColumn;
         for (int lane = 0; lane < kLanes; ++lane) {
            const float y = kLaneY[lane];
            c.setColor(alpha(0xff181b20, a));
            c.roundedRectangle(x * s, y * s, kCellW * s, kLaneH * s, 3 * s);
            c.setColor(mine ? 0xffffffff : alpha(kRule, a));
            c.roundedRectangleBorder(x * s, y * s, kCellW * s, kLaneH * s, 3 * s, mine ? 1.5f * s : 1.0f);
         }
         // Octave.
         a = dim(kOwnOctave);
         char buf[8];
         std::snprintf(buf, sizeof(buf), l.octave ? "%+d" : "%d", l.octave);
         c.setColor(alpha(l.octave ? kOrange : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, kLaneY[0] * s, kCellW * s, kLaneH * s);
         // Ratchet: the count, and P.
         a = dim(kOwnRatchet);
         std::snprintf(buf, sizeof(buf), "%d", l.ratchet);
         c.setColor(alpha(l.ratchet > 1 ? kOrange : l.pressure ? t.text : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, kLaneY[1] * s, 24 * s, kLaneH * s);
         const float px = x + 25, py = kLaneY[1] + 4;
         a = dim(kOwnPressure);
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
         a = dim(kOwnVelocity);
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
         // Chance, in percent, with a bar filled that far.
         a = dim(kOwnChance);
         std::snprintf(buf, sizeof(buf), l.chance < 100 ? "%d%%" : "%d", l.chance);
         c.setColor(alpha(l.chance < 100 ? kOrange : t.dimText, a));
         c.text(buf, ui::font(10 * s), visage::Font::kCenter, x * s, (kLaneY[3] - 1) * s, kCellW * s, kLaneH * s);
         const float cy = kLaneY[3] + kLaneH - 4;
         c.setColor(alpha(0xff3a4049, a));
         c.fill((x + 5) * s, cy * s, (kCellW - 10) * s, 1.0f * s);
         if (l.chance < 100) {
            c.setColor(alpha(kOrange, a));
            c.fill((x + 5) * s, (cy - 0.5f) * s, (kCellW - 10) * l.chance / 100.0f * s, 2.0f * s);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      const int view = static_cast<int>(std::floor((x - kGridX) / kColumn));
      col_ = view >= 0 && view < kView ? scroll_.first + view : -1;
      lane_ = -1;
      for (int l = 0; l < kLanes; ++l)
         if (y >= kLaneY[l] - 2 && y < kLaneY[l] + kLaneH + 3)
            lane_ = l;
      if (col_ < 0 || col_ >= kSteps || lane_ < 0)
         return;
      Pattern p = shown();
      const int note = noteFor(p, col_);
      downY_ = y;
      // P toggles (each click, so a quick second click toggles it back).
      if (lane_ == 1 && x - (kGridX + view * kColumn) >= 23) {
         Lane l = laneAt(p, col_, note);
         l.pressure = l.pressure ? 0 : 1;
         store(p, col_, note, l, kOwnPressure);
         commit(p);
         lane_ = -1;
         return;
      }
      if (e.repeatClickCount() >= 2) {
         // The picked notes hand the value back to their steps (on the ratchet, P with it).
         if (note >= 0)
            for (int i : pickedNotes(p))
               p.notes[i].own &= static_cast<uint8_t>(~(lane_ == 1 ? kOwnRatchet | kOwnPressure : bitFor(lane_)));
         else {
            Lane &l = p.lanes[col_];
            if (lane_ == 0)
               l.octave = 0;
            else if (lane_ == 1)
               l.ratchet = 1;
            else if (lane_ == 2)
               l.velocity = 0;
            else
               l.chance = 100;
         }
         commit(p);
         lane_ = -1;
         return;
      }
      const Lane l = laneAt(p, col_, note);
      start_ = lane_ == 0 ? l.octave : lane_ == 1 ? l.ratchet : lane_ == 2 ? l.velocity : l.chance;
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      if (lane_ < 0)
         return;
      const float s = ui::designScale(*this);
      const float dy = downY_ - e.position.y / s;
      Pattern p = shown();
      const int note = noteFor(p, col_);
      Lane l = laneAt(p, col_, note);
      if (lane_ == 0)
         l.octave = static_cast<int8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 14)), -3, 3));
      else if (lane_ == 1)
         l.ratchet = static_cast<uint8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 12)), 1, kMaxRatchet));
      else if (lane_ == 2)
         l.velocity = static_cast<int8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 2)), -64, 64));
      else
         l.chance = static_cast<uint8_t>(std::clamp(start_ + static_cast<int>(std::lround(dy / 1.5f)), 0, 100));
      store(p, col_, note, l, bitFor(lane_));
      if (p != shown())
         commit(p);
   }

   void mouseUp(const visage::MouseEvent &) override { lane_ = -1; }

   bool mouseWheel(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      const int view = static_cast<int>(std::floor((x - kGridX) / kColumn));
      int lane = -1;
      for (int l = 0; l < kLanes; ++l)
         if (y >= kLaneY[l] - 2 && y < kLaneY[l] + kLaneH + 3)
            lane = l;
      if (view < 0 || view >= kView || lane < 0 || e.wheel_delta_y == 0.0f)
         return false;
      const int col = scroll_.first + view;
      const int by = e.wheel_delta_y > 0 ? 1 : -1;
      Pattern p = shown();
      const int note = noteFor(p, col);
      Lane l = laneAt(p, col, note);
      if (lane == 0)
         l.octave = static_cast<int8_t>(std::clamp(l.octave + by, -3, 3));
      else if (lane == 1)
         l.ratchet = static_cast<uint8_t>(std::clamp(l.ratchet + by, 1, kMaxRatchet));
      else if (lane == 2)
         l.velocity = static_cast<int8_t>(std::clamp(l.velocity + 4 * by, -64, 64));
      else
         l.chance = static_cast<uint8_t>(std::clamp(l.chance + 5 * by, 0, 100));
      store(p, col, note, l, bitFor(lane));
      commit(p);
      return true;
   }

private:
   static uint8_t bitFor(int lane) {
      return lane == 0 ? kOwnOctave : lane == 1 ? kOwnRatchet : lane == 2 ? kOwnVelocity : kOwnChance;
   }
   // A picked note starting on `step`; -1 when the step's own lane is what's edited.
   int noteFor(const Pattern &p, int step) const { return scroll_.pickedOn(p, scroll_.view, step); }
   std::vector<int> pickedNotes(const Pattern &p) const { return scroll_.picked(p, scroll_.view); }
   static Lane laneAt(const Pattern &p, int step, int note) { return note >= 0 ? p.laneOf(note) : p.lanes[step]; }
   // Writes `l` back: to the step, or, editing a picked note, to every picked note as its own value
   // for `bit` (which each then keeps), so one edit sets them all.
   void store(Pattern &p, int step, int note, const Lane &l, uint8_t bit) const {
      if (note < 0) {
         p.lanes[step] = l;
         return;
      }
      for (int i : pickedNotes(p))
         storeNote(p.notes[i], l, bit);
   }
   static void storeNote(Note &n, const Lane &l, uint8_t bit) {
      n.own |= bit;
      if (bit & kOwnOctave)
         n.lane.octave = l.octave;
      if (bit & kOwnRatchet)
         n.lane.ratchet = l.ratchet;
      if (bit & kOwnPressure)
         n.lane.pressure = l.pressure;
      if (bit & kOwnVelocity)
         n.lane.velocity = l.velocity;
      if (bit & kOwnChance)
         n.lane.chance = l.chance;
   }

   void commit(const Pattern &p) {
      if (onEdit)
         onEdit(p);
      redraw();
   }

   EditorHost &host_;
   const GridScroll &scroll_;
   int col_ = -1, lane_ = -1, start_ = 0;
   float downY_ = 0.0f;
};

// A preset's patterns: a factory preset's are compiled in, a user preset's are in the file beside it.
bool presetBank(const std::string &name, Bank &bank) {
   if (const Bank *factory = factoryBank(name)) {
      bank = *factory;
      return true;
   }
   return readBank(patternFile(ui::userPresetFolder(kPresetFormat) / (name + kPresetFormat.extension)), bank);
}

// Patterns 1 to 8 in the PATTERN title row: click to select one (the Pattern param), right-click to
// copy, paste or clear it, or load a pattern from a preset into it. A letter is lit when its pattern has notes; the selected one is slate and
// the one playing has an orange bar under it.
class SlotStrip : public visage::Frame {
public:
   explicit SlotStrip(EditorHost &host) : host_(host) {}

   static constexpr float kTab = 24.0f, kGap = 3.0f;

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const int selected = slotIndex(host_.paramValue(kSlot));
      const jarp::Frame &f = host_.frame();
      for (int i = 0; i < kSlots; ++i) {
         const float x = i * (kTab + kGap);
         const bool filled = !host_.bank().slots[i].empty();
         c.setColor(i == selected ? kSlate : i == hover_ ? t.buttonHover : t.button);
         c.roundedRectangle(x * s, 1 * s, kTab * s, 22 * s, 3 * s);
         c.setColor(t.panelBorder);
         c.roundedRectangleBorder(x * s, 1 * s, kTab * s, 22 * s, 3 * s, 1.0f);
         const bool playing = f.running && f.slot == i;
         c.setColor(i == selected ? t.text : playing ? kOrange : filled ? t.text : alpha(t.dimText, 0.5f));
         c.text(std::to_string(i + 1).c_str(), ui::font(10.5f * s), visage::Font::kCenter, x * s, 1 * s, kTab * s, 22 * s);
         if (playing) {
            c.setColor(kOrange);
            c.roundedRectangle((x + 5) * s, 19 * s, (kTab - 10) * s, 2 * s, 1 * s);
         }
      }
   }

   void mouseMove(const visage::MouseEvent &e) override { setHover(at(e)); }
   void mouseExit(const visage::MouseEvent &) override { setHover(-1); }

   void mouseDown(const visage::MouseEvent &e) override {
      const int i = at(e);
      if (i < 0)
         return;
      if (e.shouldTriggerPopup()) {
         menu(i, e.position);
         return;
      }
      host_.beginEdit(kSlot);
      host_.setParam(kSlot, i + 1);
      host_.endEdit(kSlot);
      redraw();
   }

private:
   int at(const visage::MouseEvent &e) const {
      const float x = e.position.x / ui::designScale(*this);
      const int i = static_cast<int>(std::floor(x / (kTab + kGap)));
      return i >= 0 && i < kSlots && x - i * (kTab + kGap) < kTab ? i : -1;
   }
   void setHover(int i) {
      if (i != hover_) {
         hover_ = i;
         redraw();
      }
   }
   void menu(int slot, visage::Point where) {
      visage::PopupMenu m;
      m.addOption(1, std::string("Copy ") + std::to_string(slot + 1));
      m.addOption(2, std::string("Paste into ") + std::to_string(slot + 1)).enable(clipboardFull_);
      m.addOption(3, std::string("Clear ") + std::to_string(slot + 1));
      m.addBreak();
      // Load from preset: every preset with patterns, by category; a preset with more than one
      // pattern lists each. Only the pattern loads: the other slots and the settings stay.
      auto sources = std::make_shared<std::vector<Pattern>>();
      auto addPreset = [&](visage::PopupMenu &into, const std::string &name) {
         Bank bank;
         if (!presetBank(name, bank))
            return;
         int filled = 0;
         for (const Pattern &p : bank.slots)
            filled += !p.empty();
         if (filled == 0)
            return;
         if (filled == 1) {
            for (const Pattern &p : bank.slots)
               if (!p.empty()) {
                  into.addOption(kLoadId + static_cast<int>(sources->size()), name);
                  sources->push_back(p);
               }
            return;
         }
         visage::PopupMenu each(name);
         for (int k = 0; k < kSlots; ++k)
            if (!bank.slots[k].empty()) {
               each.addOption(kLoadId + static_cast<int>(sources->size()), "Pattern " + std::to_string(k + 1));
               sources->push_back(bank.slots[k]);
            }
         into.addSubMenu(std::move(each));
      };
      visage::PopupMenu load("Load from preset");
      std::vector<std::string> categories;
      for (const ui::Preset &p : factoryPresets())
         if (std::find(categories.begin(), categories.end(), p.category) == categories.end())
            categories.push_back(p.category);
      for (const std::string &cat : categories) {
         visage::PopupMenu group(cat);
         for (const ui::Preset &p : factoryPresets())
            if (p.category == cat)
               addPreset(group, p.name);
         load.addSubMenu(std::move(group));
      }
      visage::PopupMenu user("User");
      for (const ui::Preset &p : ui::userPresets(kPresetFormat))
         addPreset(user, p.name);
      if (user.hasOptions())
         load.addSubMenu(std::move(user));
      m.addSubMenu(std::move(load));
      m.onSelection() += [this, slot, sources](int id) {
         if (id == 1)
            copy(slot);
         else if (id == 2)
            paste(slot);
         else if (id == 3)
            clear(slot);
         else if (id >= kLoadId && id - kLoadId < static_cast<int>(sources->size()))
            host_.setSlot(slot, (*sources)[id - kLoadId]);
         if (onChange)
            onChange();
         redraw();
      };
      m.show(this, where);
   }

public:
   // After a slot's pattern changes here, so the grid and lanes redraw.
   std::function<void()> onChange;

   // The clipboard the menu and the panel's COPY, PASTE and CLEAR buttons share.
   void copy(int slot) {
      clipboard_ = host_.bank().slots[static_cast<size_t>(slot)];
      clipboardFull_ = true;
   }
   bool canPaste() const { return clipboardFull_; }
   void paste(int slot) {
      if (clipboardFull_)
         host_.setSlot(slot, clipboard_);
   }
   // Takes every note out, keeping the length.
   void clear(int slot) {
      Pattern empty;
      empty.length = host_.bank().slots[static_cast<size_t>(slot)].length;
      host_.setSlot(slot, empty);
   }

private:
   static constexpr int kLoadId = 100;
   EditorHost &host_;
   int hover_ = -1;
   Pattern clipboard_;
   bool clipboardFull_ = false;
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
      c.text("RANDOM PRESET", ui::font(9.5f * s), visage::Font::kLeft, 16 * s, 376 * s, 120 * s, 16 * s);
      c.text("the whole preset, or one pattern", ui::font(9 * s), visage::Font::kRight, 0, 376 * s, (width() / s - 16) * s, 16 * s);
      c.setColor(kRule);
      c.fill(16 * s, 394 * s, (width() / s - 32) * s, 1.0f * s);
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
         // A factory preset's patterns are compiled in; a user preset's are in the file beside it.
         Bank bank;
         if (!presetBank(p.name, bank))
            return;
         host_.setBank(bank);
         grid_->redraw();
         lanes_->redraw();
      };
      presets_->onSaved = [this](const std::filesystem::path &file) { writeBank(patternFile(file), host_.bank()); };
      presets_->onDeleted = [](const std::filesystem::path &file) {
         std::error_code ec;
         std::filesystem::remove(patternFile(file), ec);
      };
      presets_->onDice = [this] { showDice(!host_.diceOpen); };
      presets_->diceLit = [this] { return host_.diceOpen; };
      tips_.add(*presets_, "Presets: the arrows step through them, the name opens the menu, the star marks a favorite and "
                           "SAVE keeps the current settings and all eight patterns. The die opens the dice.");

      // PATTERN: the grid and its lanes, with how rows are picked in its title row.
      add<ui::Section>({12, 76, 876, 478}, "PATTERN");
      slots_ = &add<SlotStrip>({118, 88, 213, 24}, host_);
      buildClipboard();
      slots_->onChange = [this] {
         grid_->redraw();
         lanes_->redraw();
      };
      tips_.add(*slots_, "Patterns 1 to 8: click one to play and edit it (the Pattern param, which the host can "
                         "automate). Lit numbers have notes; the orange bar is the one playing. Right-click to copy, "
                         "paste, clear, or load a pattern from a preset into it. Empty patterns are skipped when the chain "
                         "advances.");
      tips_.add(choice(kAdvance, {340, 89, 128, 22}, {"OFF", "CHORD", "LOOP"}),
                "Advance: Off plays the pattern selected. Chord moves to the next pattern with notes each time you "
                "play a new chord after letting go of every note. Loop moves on each time the pattern ends.");
      rowsLabel(470, "ROWS BY");
      auto &rowsBy = add<ui::Dropdown>({552, 89, 92, 22}, std::vector<std::string>{"PITCH", "DEGREE", "HELD DEGREE", "SPREAD"});
      rowsBy.setAccent(kSlate);
      bindings_.bind(rowsBy, kRowsBy);
      tips_.add(rowsBy, "Pitch: row 1 is the lowest note held, row 2 the next up, and so on. Degree: rows count up the "
                        "scale from the lowest note held; a degree that isn't held plays the nearest note that is. Held "
                        "Degree: the same, but a degree that isn't held stays silent. Spread: the pattern's bottom row plays "
                        "the lowest note held and its top row the highest, the rows between spread over the notes between.");
      emptyLabel_ = &rowsLabel(669, "EMPTY ROWS");
      empty_ = &choice(kEmptyRows, {752, 89, 120, 22}, {"REST", "WRAP", "OCT"});
      tips_.add(*empty_, "Rows above the notes held: Rest stays silent; Wrap starts again from the lowest note; Oct "
                         "starts again an octave up each time, like an arpeggiator's octave range.");
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

      grid_ = &add<GridView>({20, 116, 862, 324}, host_, scroll_);
      grid_->onScroll = [this] { lanes_->redraw(); };
      grid_->onEdit = [this](const Pattern &p) { edit(p); };
      tips_.add(*grid_, "Click a cell for a note and drag right to lengthen it; drag a note's right edge to set how "
                        "long it sounds; drag a note to move it; click a note to remove it. Right-click a note to give it lane "
                        "values of its own; shift-click to pick more, or right-drag a box around them. Dragging a picked note "
                        "moves them all. Drag the purple bar to set the pattern's length.");
      add<ui::Divider>({27, 445, 846, 1});
      lanes_ = &add<LaneView>({20, 450, 862, 96}, host_, scroll_);
      lanes_->onEdit = [this](const Pattern &p) { edit(p); };
      tips_.add(*lanes_, "Per step, for the notes starting on it: drag up or down, scroll, or double-click to reset. "
                         "Click P to make that step's ratchet follow pressure. Chance is how often the step plays, in percent. With notes picked (right-click them), "
                         "a picked note's column sets the own values of every picked note; double-click gives one back to the step.");

      // Settings.
      add<SettingsCard>({12, 568, 876, 142});
      tips_.add(knob(kRate, "RATE", kOrange, 37, 606), "How long each step is.");
      tips_.add(knob(kSwing, "SWING", kOrange, 107, 606), "Moves every second step later: 50% is straight.");
      length_ = &add<ui::Knob>({177, 606, 76, 88}, ui::ValueRange{1, kSteps, 8, 1.0, false, true});
      length_->setLabel("LENGTH");
      length_->setAccent(kOrange);
      length_->setLabelColor(ui::defaultTheme().dimText);
      length_->valueText = [](double v) {
         const int n = static_cast<int>(std::lround(v));
         return std::to_string(n) + (n == 1 ? " step" : " steps");
      };
      length_->onChange = [this](double v) {
         Pattern p = shown();
         p.length = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, kSteps));
         edit(p);
      };
      tips_.add(*length_, "How many steps this pattern plays before it loops; each pattern has its own. The purple bar "
                          "on the grid is the same.");
      heading("LATCH", 270, 606);
      tips_.add(choice(kLatch, {270, 624, 80, 22}, {"OFF", "ON"}),
                "On: the notes keep playing after you let go, until you play a new chord.");
      heading("RANGE", 360, 606);
      auto &range = add<ui::Dropdown>({360, 624, 50, 22}, std::vector<std::string>{"1 OCT", "2 OCT", "3 OCT", "4 OCT"});
      range.setAccent(kSlate);
      bindings_.bind(range, kRange);
      tips_.add(range, "How far above the lowest note you hold a note may be moved up by octaves (by OCT rows or the "
                       "Octave lane). A note that would go higher drops back down by octaves until it fits.");
      heading("RESTART", 270, 652);
      tips_.add(choice(kRestart, {270, 670, 138, 22}, {"SONG", "NOTE", "BAR"}),
                "Where the pattern starts: Song locks it to the song position, so a chord played half way through "
                "the pattern starts half way through it (and a Loop chain plays along the song); Note starts it "
                "with the first note you play; Bar starts it at the bar the first note is in.");
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
      {
         const jarp::Frame &f = host_.frame();
         scroll_.track(slotIndex(host_.paramValue(kSlot)), f.running ? f.slot : -1);
      }
      const int rowsBy = static_cast<int>(std::lround(host_.paramValue(kRowsBy)));
      const bool degree = rowsBy == 1 || rowsBy == 2;
      emptyLabel_->setVisible(rowsBy == 0);
      empty_->setVisible(rowsBy == 0);
      scaleLabel_->setVisible(degree);
      key_->setVisible(degree);
      scale_->setVisible(degree);
      if (std::lround(length_->value()) != shown().length)
         length_->setValue(shown().length);
      {
         const jarp::Frame &f = host_.frame();
         const int len = std::clamp<int>(shown().length, 1, kSteps);
         const bool here = f.running && f.slot == scroll_.view;
         scroll_.update(len, here ? std::clamp(static_cast<int>(f.position), 0, len - 1) : -1);
      }
      slots_->redraw();
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

   // Adds the note starting on (row, step) of the selected slot to the picked notes, as a
   // shift-click on it would.
   void pickNote(int row, int step) {
      const int i = shown().noteStarting(row, step);
      if (i >= 0)
         scroll_.toggle(scroll_.view, shown().notes[i]);
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
      host_.setSlot(scroll_.view, p);
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

   static constexpr ui::Rect kDiceBox{523, 71, 358, 484};
   // The random preset row, from the card's top.
   static constexpr float kPresetY = 404;

   // COPY, PASTE and CLEAR for the pattern on show, in the header.
   void buildClipboard() {
      auto &copy = add<ui::Button>({356, 18, 56, 28}, "COPY");
      copy.onClick = [this] {
         slots_->copy(scroll_.view);
         paste_->setAccent(kOrange);
         paste_->redraw();
      };
      tips_.add(copy, "Copies the pattern on show, to paste into another slot.");
      paste_ = &add<ui::Button>({416, 18, 58, 28}, "PASTE");
      paste_->onClick = [this] {
         if (!slots_->canPaste())
            return;
         host_.diceHistory.push(DiceState(shown(), scroll_.view));
         slots_->paste(scroll_.view);
         slotEdited();
      };
      tips_.add(*paste_, "Replaces the pattern on show with the one last copied (lit once there is one).");
      auto &clear = add<ui::Button>({478, 18, 56, 28}, "CLEAR");
      clear.onClick = [this] {
         host_.diceHistory.push(DiceState(shown(), scroll_.view));
         slots_->clear(scroll_.view);
         slotEdited();
      };
      tips_.add(clear, "Takes every note out of the pattern on show, keeping its length. The dice card's UNDO brings "
                       "it back.");
   }

   void slotEdited() {
      scroll_.unpick();
      slots_->redraw();
      grid_->redraw();
      lanes_->redraw();
      refreshDice();
   }

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

      // A random preset: the whole of it, or one of its patterns into this slot.
      const float py = b.y + kPresetY;
      std::vector<std::string> cats{"Any category"};
      for (const std::string &c : factoryCategories())
         cats.push_back(c);
      auto &cat = add<ui::Dropdown>({b.x + 16, py, 150, 26}, cats);
      cat.setAccent(kOrange);
      cat.setSelected(std::clamp(host_.dice.presetCategory, 0, static_cast<int>(cats.size()) - 1));
      cat.onChange = [this](int i) { host_.dice.presetCategory = i; };
      keep(cat);
      tips_.add(cat, "Where a random preset comes from: any factory preset, or one category.");
      auto &all = add<ui::Button>({b.x + 174, py, 80, 26}, "PRESET");
      all.setAccent(kOrange);
      all.onClick = [this] { randomPreset(false); };
      keep(all);
      tips_.add(all, "Loads a random factory preset: its settings and all its patterns. UNDO brings back what you had.");
      auto &one = add<ui::Button>({b.x + 260, py, 82, 26}, "INTO SLOT");
      one.setAccent(kOrange);
      one.onClick = [this] { randomPreset(true); };
      keep(one);
      tips_.add(one, "Puts one random pattern from the factory presets into the selected slot, leaving the rest as it is.");
   }

   void roll(bool mutate) {
      const Pattern now = shown();
      const uint64_t seed = DiceRng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                    ++diceSeed_)
                               .next();
      const int len = shown().length;
      const Pattern next = mutate ? mutatePattern(now, host_.dice, len, seed) : randomizePattern(now, host_.dice, len, seed);
      host_.diceHistory.push(DiceState(now, selectedSlot()));
      edit(next);
      refreshDice();
   }

   // The slot on show, which edits and the dice go to.
   int selectedSlot() const { return scroll_.view; }

   // A random factory preset, from the category picked on the card (or any): the whole of it, or one
   // of its patterns into the selected slot.
   void randomPreset(bool intoSlot) {
      const int category = host_.dice.presetCategory;
      const std::string current = host_.presetName();
      std::vector<const library::Entry *> pool;
      for (const library::Entry &e : factoryLibrary())
         if (category <= 0 || category > static_cast<int>(factoryCategories().size()) ||
             e.category == factoryCategories()[category - 1])
            pool.push_back(&e);
      // Loading the one already loaded would look like nothing happened.
      if (!intoSlot && pool.size() > 1)
         pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const library::Entry *e) { return e->name == current; }),
                    pool.end());
      if (pool.empty())
         return;
      DiceRng rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) + ++diceSeed_);
      const library::Entry &pick = *pool[static_cast<size_t>(rng.range(0, static_cast<int>(pool.size()) - 1))];
      if (intoSlot) {
         std::vector<const Pattern *> patterns;
         for (const Pattern &p : pick.patterns)
            if (!p.empty())
               patterns.push_back(&p);
         if (patterns.empty())
            return;
         const Pattern &p = *patterns[static_cast<size_t>(rng.range(0, static_cast<int>(patterns.size()) - 1))];
         host_.diceHistory.push(DiceState(shown(), selectedSlot()));
         host_.setSlot(selectedSlot(), p);
      }
      else {
         host_.diceHistory.push(capture(true, 0));
         presets_->load(pick.name);
      }
      grid_->redraw();
      lanes_->redraw();
      refreshDice();
   }

   // Now, as the same kind of thing a history step holds.
   DiceState capture(bool whole, int slot) const {
      if (!whole)
         return DiceState(host_.bank().slots[slot], slot);
      DiceState::Whole w;
      for (clap_id id = 0; id < kParamCount; ++id)
         w.values.push_back(host_.paramValue(id));
      w.bank = host_.bank();
      w.name = host_.presetName();
      return DiceState(std::move(w));
   }

   void restore(const DiceState &st) {
      if (st.whole) {
         for (clap_id id = 0; id < kParamCount && id < st.whole->values.size(); ++id)
            if (host_.paramValue(id) != st.whole->values[id]) {
               host_.beginEdit(id);
               host_.setParam(id, st.whole->values[id]);
               host_.endEdit(id);
            }
         host_.setBank(st.whole->bank);
         host_.setPresetName(st.whole->name);
         if (presets_)
            presets_->redraw();
      }
      else
         host_.setSlot(st.slot, st.pattern);
      grid_->redraw();
      lanes_->redraw();
   }

   void stepHistory(bool forward) {
      DiceHistory &h = host_.diceHistory;
      if (forward ? !h.canRedo() : !h.canUndo())
         return;
      const DiceState &next = forward ? h.nextRedo() : h.nextUndo();
      const DiceState now = capture(next.whole != nullptr, next.slot);
      restore(forward ? h.redo(now) : h.undo(now));
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
   GridScroll scroll_;

   const Pattern &shown() const { return host_.bank().slots[static_cast<size_t>(scroll_.view)]; }
   GridView *grid_ = nullptr;
   SlotStrip *slots_ = nullptr;
   ui::Knob *length_ = nullptr;
   LaneView *lanes_ = nullptr;
   ui::Label *emptyLabel_ = nullptr, *scaleLabel_ = nullptr;
   ui::SegmentedSwitch *empty_ = nullptr;
   ui::Dropdown *key_ = nullptr, *scale_ = nullptr;
   DiceCard *card_ = nullptr;
   ui::Button *undo_ = nullptr, *redo_ = nullptr;
   ui::Button *paste_ = nullptr;
   std::vector<visage::Frame *> dice_;
   uint64_t diceSeed_ = 0;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, bool dice,
                      const std::vector<std::pair<int, int>> &picks) {
   Panel panel(host);
   panel.stopTimer();
   panel.showDice(dice);
   for (const auto &[row, step] : picks)
      panel.pickNote(row, step);
   panel.setBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
   panel.sync();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jarp
