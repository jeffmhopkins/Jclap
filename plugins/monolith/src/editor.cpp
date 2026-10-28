// Monolith's panel, on a 1200 x 680 grid in black and ember:
//
//    header   the name, the preset bar, SEED with REROLL, LOCK and RESTORE
//    left     how the glyphs sound and are read: script, key, rows, rate, direction, mode, output
//    centre   the slab: 55 glyphs read top to bottom, columns right to left. Click one to start
//             reading there (and decode it; click again to follow the voice), drag across some to
//             read just those, alt-click to read one next, shift-click to have the scribe write it
//             again.
//    right    the decode view: the glyph large on its node grid, strokes numbered in the order they
//             play, and the notes they read as. Drag from node to node to carve a stroke;
//             right-click a stroke to wipe it. Under it the phrase being read, and the knobs.
//
// In LISTEN the whole panel gives way to the stage: a display with no controls, for capturing on a
// stream (see StageView).

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/note_roll.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "glyph.h"
#include "params.h"
#include "presets.h"

namespace monolith {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1200.0f;
constexpr float kDesignHeight = 680.0f;
constexpr float kPi = 3.14159265358979f;

constexpr unsigned kBackground = 0xff0b0a0a;
constexpr unsigned kStone = 0xff141211;
constexpr unsigned kEdge = 0xff2a2421;
constexpr unsigned kText = 0xffe8e4dc;
constexpr unsigned kDim = 0xff6b645c;
constexpr unsigned kEmber = 0xffff5a36;
constexpr unsigned kAsh = 0xff3a3330;
// The hot core of a lit stroke, inside its ember glow.
constexpr unsigned kPeach = 0xffffd6c4;

unsigned withAlpha(unsigned color, float alpha) {
   return (color & 0x00ffffffu) | (static_cast<unsigned>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) << 24);
}

const ui::Theme &emberTheme() {
   static const ui::Theme theme = [] {
      ui::Theme t;
      t.background = kBackground;
      t.panel = kStone;
      t.panelBorder = kEdge;
      t.headerBar = 0xff0f0d0d;
      t.accent = kEmber;
      t.text = kText;
      t.dimText = kDim;
      t.track = 0xff2a2421;
      t.modulation = 0xffffd2b8;
      t.button = 0xff1a1716;
      t.buttonHover = 0xff262120;
      t.knobLight = kAsh;
      t.knobDark = 0xff171413;
      t.knobEdge = kBackground;
      return t;
   }();
   return theme;
}

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, false,
           (kParams[id].flags & CLAP_PARAM_IS_STEPPED) != 0};
}

std::vector<std::string> namesOf(clap_id id) {
   int count = 0;
   const char *const *names = choiceNames(id, count);
   return std::vector<std::string>(names, names + count);
}

// ---- drawing glyphs ----

// Where a glyph's grid sits in a box: the nodes' grid units (x 0..2, y 0..3) to pixels, keeping
// the shape and leaving room for the overbar. Every script's nodes fit in the same grid.
struct GlyphBox {
   float x0, y0, unit;

   static GlyphBox fit(float x, float y, float w, float h) {
      const float unit = std::min(w / 2.9f, h / 4.2f);
      const float left = x + (w - 2.0f * unit) * 0.5f;
      const float top = y + (h - 3.0f * unit) * 0.5f + 0.25f * unit;
      return {left, top, unit};
   }
   Point at(Point p) const { return {x0 + p.x * unit, y0 + p.y * unit}; }
   Point node(Orthography o, int n) const { return at(nodePosition(o, n)); }
};

void drawStroke(visage::Canvas &c, const GlyphBox &box, Orthography o, Stroke s, float t) {
   const Point a = box.node(o, s.from), b = box.node(o, s.to);
   switch (styleOf(o)) {
   case StrokeStyle::Line: c.segment(a.x, a.y, b.x, b.y, t, false); break;
   case StrokeStyle::Rune: {
      c.segment(a.x, a.y, b.x, b.y, t * 1.5f, false);
      // A short serif across the start, cut at an angle.
      const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
      const float nx = -dy / len, ny = dx / len, k = t * 2.2f;
      c.segment(a.x - nx * k - dx / len * k * 0.4f, a.y - ny * k - dy / len * k * 0.4f, a.x + nx * k + dx / len * k * 0.4f,
                a.y + ny * k + dy / len * k * 0.4f, t, false);
      break;
   }
   case StrokeStyle::Trace: {
      // Across, then down: a right angle where the stroke isn't straight already.
      const Point corner{b.x, a.y};
      c.segment(a.x, a.y, corner.x, corner.y, t, true);
      c.segment(corner.x, corner.y, b.x, b.y, t, true);
      const float pad = t * 3.2f;
      c.ring(a.x - pad * 0.5f, a.y - pad * 0.5f, pad, t * 0.8f);
      c.circle(b.x - pad * 0.4f, b.y - pad * 0.4f, pad * 0.8f);
      break;
   }
   case StrokeStyle::Arc: {
      if (rowOf(s.from) == rowOf(s.to)) {
         const Point centre = box.at({kHaloCenterX, kHaloCenterY});
         const float r = haloRadius(rowOf(s.from)) * box.unit;
         const float a1 = haloAngle(s.from), a2 = haloAngle(s.to);
         const float d = 2.0f * r + t;
         c.arc(centre.x - d * 0.5f, centre.y - d * 0.5f, d, t, (a1 + a2) * 0.5f - kPi * 0.5f, std::fabs(a1 - a2) * 0.5f,
               true);
      }
      else
         c.segment(a.x, a.y, b.x, b.y, t, true);
      break;
   }
   case StrokeStyle::Wedge: {
      const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
      const float nx = -dy / len, ny = dx / len, head = t * 1.9f;
      c.triangle(a.x + nx * head, a.y + ny * head, a.x - nx * head, a.y - ny * head, b.x, b.y);
      // The stylus's head, pressed in where the wedge starts.
      c.triangle(a.x + nx * head, a.y + ny * head, a.x - nx * head, a.y - ny * head, a.x - dx / len * head * 1.2f,
                 a.y - dy / len * head * 1.2f);
      break;
   }
   }
}

// The point a fraction `t` of the way along a stroke as its script draws it: along the corner of a
// trace, around a ring for a Halo arc, straight otherwise.
Point strokePoint(const GlyphBox &box, Orthography o, Stroke s, float t) {
   const Point a = box.node(o, s.from), b = box.node(o, s.to);
   t = std::clamp(t, 0.0f, 1.0f);
   const auto lerp = [](Point p, Point q, float k) { return Point{p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k}; };
   switch (styleOf(o)) {
   case StrokeStyle::Trace: {
      const Point corner{b.x, a.y};
      const float across = std::fabs(b.x - a.x), down = std::fabs(b.y - a.y), total = across + down;
      if (total <= 0.0f)
         return a;
      const float d = t * total;
      return d <= across ? lerp(a, corner, across > 0.0f ? d / across : 0.0f) : lerp(corner, b, (d - across) / down);
   }
   case StrokeStyle::Arc:
      if (rowOf(s.from) == rowOf(s.to)) {
         const Point centre = box.at({kHaloCenterX, kHaloCenterY});
         const float r = haloRadius(rowOf(s.from)) * box.unit;
         const float angle = haloAngle(s.from) + (haloAngle(s.to) - haloAngle(s.from)) * t;
         return {centre.x + r * std::sin(angle), centre.y - r * std::cos(angle)};
      }
      return lerp(a, b, t);
   default: return lerp(a, b, t);
   }
}

struct GlyphStyle {
   // Unlit strokes. While the glyph plays, the ones still to come.
   unsigned color = kText;
   // While the glyph plays: how far into its 16 steps the voice is (negative when it isn't playing).
   // Each stroke lights as its note sounds, with a pen point running along it, and stays lit after.
   float step = -1.0f;
   unsigned lit = kEmber;
   // How brightly the whole glyph burns when it isn't playing stroke by stroke: 0 drawn plain in
   // `color`, 1 fully lit like a sounding stroke.
   float glow = 0.0f;
   // Eroded strokes are drawn as faint scars in this colour; 0 leaves them out.
   unsigned scar = 0;
   float thickness = 2.0f;
   bool nodes = false;
   bool numbers = false;
};

// A stroke burning at `heat` (0..1): soft ember halos widening around a peach core.
void drawLitStroke(visage::Canvas &c, const GlyphBox &box, Orthography o, Stroke s, float t, unsigned lit, float heat) {
   const float halos[][2] = {{7.0f, 0.05f}, {4.5f, 0.09f}, {2.8f, 0.18f}, {1.8f, 0.35f}};
   for (const auto &h : halos) {
      c.setColor(withAlpha(lit, h[1] * heat));
      drawStroke(c, box, o, s, t * h[0]);
   }
   c.setColor(withAlpha(kPeach, 0.35f + 0.65f * heat));
   drawStroke(c, box, o, s, t);
}

void drawGlyph(visage::Canvas &c, const Glyph &g, int visible, Orthography o, const GlyphBox &box, const GlyphStyle &style) {
   const float t = style.thickness;
   if (style.nodes) {
      if (o == Orthography::Halo) {
         c.setColor(withAlpha(kEdge, 0.9f));
         const Point centre = box.at({kHaloCenterX, kHaloCenterY});
         for (int r = 0; r < kGridRows; ++r) {
            const float d = 2.0f * haloRadius(r) * box.unit + 1.0f;
            c.arc(centre.x - d * 0.5f, centre.y - d * 0.5f, d, 1.0f, -kPi * 0.5f, haloSpread(r) * 1.5f, false);
         }
      }
      for (int n = 0; n < kNodes; ++n) {
         const Point p = box.node(o, n);
         const float d = std::max(4.0f, t * 2.2f);
         c.setColor(kEdge);
         c.circle(p.x - d * 0.5f, p.y - d * 0.5f, d);
      }
   }
   if (style.scar)
      for (int i = visible; i < g.count; ++i) {
         c.setColor(style.scar);
         drawStroke(c, box, o, g.strokes[i], t * 0.6f);
      }
   // Each stroke's note, for lighting them in time. A stroke past the window never plays.
   float progress[kMaxStrokes];
   float dotProgress = -1.0f;
   for (float &p : progress)
      p = -1.0f;
   if (style.step >= 0.0f) {
      const GlyphNotes notes = decode(g, visible);
      for (int i = 0; i < notes.count; ++i) {
         const GlyphNote &n = notes.notes[i];
         const float p = (style.step - n.start) / static_cast<float>(std::max(1, n.length));
         if (n.ghost)
            dotProgress = p;
         else
            progress[n.stroke] = p;
      }
   }
   for (int i = 0; i < visible && i < g.count; ++i) {
      const float p = progress[i];
      if (p >= 0.0f && p < 1.0f)
         drawLitStroke(c, box, o, g.strokes[i], t, style.lit, 1.0f);
      else if (p >= 1.0f)
         drawLitStroke(c, box, o, g.strokes[i], t, style.lit, 0.55f);
      else if (style.step < 0.0f && style.glow > 0.0f)
         drawLitStroke(c, box, o, g.strokes[i], t, style.lit, style.glow);
      else {
         c.setColor(style.color);
         drawStroke(c, box, o, g.strokes[i], t);
      }
   }
   // The pen points of the sounding strokes, on top.
   for (int i = 0; i < visible && i < g.count; ++i)
      if (progress[i] >= 0.0f && progress[i] < 1.0f) {
         const Point p = strokePoint(box, o, g.strokes[i], progress[i]);
         const float d = t * 4.0f;
         c.setColor(withAlpha(style.lit, 0.35f));
         c.circle(p.x - d, p.y - d, d * 2.0f);
         c.setColor(0xffffffffu);
         c.circle(p.x - d * 0.4f, p.y - d * 0.4f, d * 0.8f);
      }
   if (g.dot >= 0 && g.dot < kNodes) {
      const Point p = box.node(o, g.dot);
      const float d = t * 2.6f;
      const bool sounding = dotProgress >= 0.0f && dotProgress < 1.0f;
      const bool burning = sounding || (style.step < 0.0f && style.glow > 0.0f);
      if (burning || dotProgress >= 1.0f) {
         c.setColor(withAlpha(style.lit, 0.2f * (sounding ? 1.0f : 0.6f)));
         c.circle(p.x - d * 1.4f, p.y - d * 1.4f, d * 2.8f);
         c.setColor(kPeach);
      }
      else
         c.setColor(style.color);
      if (burning)
         c.circle(p.x - d * 0.5f, p.y - d * 0.5f, d);
      else
         c.ring(p.x - d * 0.5f, p.y - d * 0.5f, d, std::max(1.0f, t * 0.7f));
   }
   if (g.bar) {
      const Point l = box.at({-0.1f, -0.55f}), r = box.at({2.1f, -0.55f});
      const float heat = style.step >= 0.0f ? 0.55f : style.glow;
      if (heat > 0.0f) {
         c.setColor(withAlpha(style.lit, 0.18f * heat));
         c.segment(l.x, l.y, r.x, r.y, t * 3.5f, true);
         c.setColor(withAlpha(kPeach, 0.35f + 0.65f * heat));
      }
      else
         c.setColor(style.color);
      c.segment(l.x, l.y, r.x, r.y, t, false);
   }
   if (style.numbers)
      for (int i = 0; i < visible && i < g.count; ++i) {
         const Point a = box.node(o, g.strokes[i].from), b = box.node(o, g.strokes[i].to);
         const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
         const float mx = a.x + dx * 0.5f - dy / len * box.unit * 0.18f, my = a.y + dy * 0.5f + dx / len * box.unit * 0.18f;
         const float d = box.unit * 0.26f;
         c.setColor(kBackground);
         c.circle(mx - d * 0.5f, my - d * 0.5f, d);
         c.setColor(kEmber);
         c.ring(mx - d * 0.5f, my - d * 0.5f, d, std::max(1.0f, d * 0.08f));
         c.setColor(kText);
         c.text(std::to_string(i + 1), ui::font(d * 0.62f), visage::Font::kCenter, mx - d * 0.5f, my - d * 0.5f, d, d);
      }
}

// ---- the slab ----

// Cell i sits in column 4 - i / 11 (the first column is on the right) and row i % 11.
inline void cellPlace(int i, int &column, int &row) {
   column = kSlabColumns - 1 - i / kSlabRows;
   row = i % kSlabRows;
}

class SlabView : public visage::Frame {
   // Room either side of the slab's face, and under it for the ground.
   static constexpr float kSide = 10.0f, kBase = 22.0f;

public:
   explicit SlabView(const SlabFrame &frame) : frame_(frame) {}

   std::function<void(int)> onSelect, onJump, onReroll;
   // A click or drag across cells: the first cell and how many, as the press begins, moves and ends.
   std::function<void(int, int)> onBeginSpan, onSpan, onEndSpan;
   int selected = -1;

   // The slab stands as a block of dark stone on a faint ember ground; the glyphs are cut into its
   // face, the most recently read burning and the rest pale, fading toward the columns read last.
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float faceX = kSide, faceW = w - 2 * kSide, faceH = h - kBase;
      const float cw = faceW / kSlabColumns, ch = faceH / kSlabRows;

      // Ground glow and the slab's shadow on it.
      c.setColor(visage::Brush::horizontal(visage::Color(0x00ff5a36), visage::Color(0x33ff5a36)));
      c.fill(0, h - kBase * 0.75f, w * 0.5f, kBase * 0.5f);
      c.setColor(visage::Brush::horizontal(visage::Color(0x33ff5a36), visage::Color(0x00ff5a36)));
      c.fill(w * 0.5f, h - kBase * 0.75f, w * 0.5f, kBase * 0.5f);
      c.setColor(0xcc000000);
      c.fill(faceX - 4, faceH, faceW + 8, kBase * 0.35f);

      // The face: lighter at the top, as if lit from above, with a bright left edge.
      c.setColor(visage::Brush::vertical(visage::Color(0xff191615), visage::Color(0xff0c0b0b)));
      c.fill(faceX, 0, faceW, faceH);
      c.setColor(0x14ffffff);
      c.fill(faceX, 0, 1.5f, faceH);
      c.setColor(0x66000000);
      c.fill(faceX + faceW - 2, 0, 2, faceH);
      for (int k = 1; k < kSlabColumns; ++k) {
         c.setColor(0x0affffff);
         c.fill(faceX + k * cw, ch * 0.4f, 1, faceH - ch * 0.8f);
      }

      for (int i = 0; i < kCells; ++i) {
         int column, row;
         cellPlace(i, column, row);
         const float x = faceX + column * cw, y = row * ch;
         const bool inUse = inWindow(i, frame_.start, frame_.length);
         const bool current = frame_.playing && i == frame_.current;
         float trail = 0.0f;
         for (int k = 0; k < kHistory; ++k)
            if (frame_.history[k] == i && frame_.playing)
               trail = std::max(trail, (k + 1) / static_cast<float>(kHistory));
         // Columns read later are more worn: the first (right) is clearest.
         const float wear = 1.0f - 0.12f * static_cast<float>(i / kSlabRows);
         const Cell &cell = frame_.cells[i];
         GlyphStyle style;
         style.thickness = std::max(1.2f, ch * 0.042f);
         style.color = current ? withAlpha(kText, 0.35f)
                       : inUse ? withAlpha(kText, (0.5f + 0.3f * trail) * wear)
                               : withAlpha(kDim, 0.3f * wear);
         style.step = current ? frame_.step : -1.0f;
         style.glow = current ? 0.0f : trail > 0.0f && inUse ? 0.25f * trail : 0.0f;
         style.scar = inUse ? withAlpha(kDim, 0.25f * wear) : 0;
         const GlyphBox box = GlyphBox::fit(x + 6, y + 5, cw - 12, ch - 12);
         if (current) {
            const float d = std::min(cw, ch) * 1.3f;
            c.setColor(visage::Brush::radial(visage::Color(0x30ff5a36), visage::Color(0x00ff5a36),
                                             {x + cw * 0.5f, y + ch * 0.5f}, d * 0.5f));
            c.fill(x + cw * 0.5f - d * 0.5f, y + ch * 0.5f - d * 0.5f, d, d);
         }
         drawGlyph(c, cell.glyph, cell.visible(), frame_.orthography, box, style);
         if (current) {
            c.setColor(withAlpha(kEmber, 0.3f));
            c.fill(x + cw * 0.28f - 1, y + ch - 6, cw * 0.44f + 2, 4);
            c.setColor(kEmber);
            c.fill(x + cw * 0.28f, y + ch - 5, cw * 0.44f, 2);
         }
         if (i == selected) {
            c.setColor(withAlpha(kPeach, 0.8f));
            const float l = 6, x0 = x + 3, y0 = y + 2, x1 = x + cw - 3, y1 = y + ch - 2;
            c.fill(x0, y0, l, 1);
            c.fill(x0, y0, 1, l);
            c.fill(x1 - l, y0, l, 1);
            c.fill(x1 - 1, y0, 1, l);
            c.fill(x0, y1 - 1, l, 1);
            c.fill(x0, y1 - l, 1, l);
            c.fill(x1 - l, y1 - 1, l, 1);
            c.fill(x1 - 1, y1 - l, 1, l);
         }
      }
      // The lines where the read part starts and ends.
      const auto edge = [&](int i) {
         int column, row;
         cellPlace(i, column, row);
         c.setColor(withAlpha(kEmber, 0.35f));
         c.fill(faceX + column * cw + 8, row * ch, cw - 16, 1.0f);
      };
      if (frame_.start > 0)
         edge(frame_.start);
      if (frame_.length < kCells && (frame_.start + frame_.length) % kCells != 0)
         edge((frame_.start + frame_.length) % kCells);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const int i = cellAt(e.position.x, e.position.y);
      if (i < 0)
         return;
      if (e.isShiftDown()) {
         if (onReroll)
            onReroll(i);
      }
      else if (e.isAltDown()) {
         if (onJump)
            onJump(i);
      }
      else {
         anchor_ = last_ = i;
         if (onBeginSpan)
            onBeginSpan(i, 1);
      }
   }

   // Dragging spans the cells from the one pressed to the one under the mouse, in reading order.
   void mouseDrag(const visage::MouseEvent &e) override {
      if (anchor_ < 0)
         return;
      const int i = cellAt(e.position.x, e.position.y, true);
      if (i == last_)
         return;
      last_ = i;
      if (onSpan)
         onSpan(std::min(anchor_, i), std::abs(i - anchor_) + 1);
   }

   void mouseUp(const visage::MouseEvent &) override {
      if (anchor_ < 0)
         return;
      const int first = std::min(anchor_, last_), count = std::abs(last_ - anchor_) + 1;
      anchor_ = last_ = -1;
      if (onEndSpan)
         onEndSpan(first, count);
      if (count == 1 && onSelect)
         onSelect(first);
   }

   // The cell at a point, or -1 off the face; with `nearest`, the nearest cell to a point off it.
   int cellAt(float px, float py, bool nearest = false) const {
      const float faceW = width() - 2 * kSide, faceH = height() - kBase;
      if (nearest) {
         px = std::clamp(px, kSide, kSide + faceW - 1);
         py = std::clamp(py, 0.0f, faceH - 1);
      }
      if (px < kSide || py >= faceH)
         return -1;
      const int column = static_cast<int>((px - kSide) / (faceW / kSlabColumns));
      const int row = static_cast<int>(py / (faceH / kSlabRows));
      if (column < 0 || column >= kSlabColumns || row < 0 || row >= kSlabRows)
         return -1;
      return (kSlabColumns - 1 - column) * kSlabRows + row;
   }

private:
   const SlabFrame &frame_;
   int anchor_ = -1, last_ = -1;
};

// ---- the decode view ----

class DecodeView : public visage::Frame {
public:
   explicit DecodeView(const SlabFrame &frame) : frame_(frame) {}

   // The cell shown; set by the panel each tick.
   int cell = 0;
   std::function<void(const Glyph &)> onCarve;

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      c.setColor(kBackground);
      c.roundedRectangle(0, 0, w, h, 6);
      const Cell &x = frame_.cells[cell];
      GlyphStyle style;
      style.thickness = std::max(2.5f, h * 0.018f);
      style.color = kText;
      style.step = frame_.playing && cell == frame_.current ? frame_.step : -1.0f;
      style.glow = 0.8f;
      style.color = withAlpha(kText, 0.45f);
      style.scar = withAlpha(kDim, 0.5f);
      style.nodes = true;
      style.numbers = true;
      // Each row's note beside it.
      const GlyphBox b = box();
      for (int r = 0; r < kGridRows; ++r) {
         const Point p = b.node(frame_.orthography, nodeAt(0, r));
         c.setColor(kDim);
         c.text(ui::noteName(frame_.rowPitch[r]), ui::font(11), visage::Font::kRight, p.x - b.unit * 0.3f - 40, p.y - 8, 40, 16);
      }
      drawGlyph(c, x.glyph, x.visible(), frame_.orthography, b, style);
      if (dragFrom_ >= 0) {
         const Point a = box().node(frame_.orthography, dragFrom_);
         c.setColor(withAlpha(kEmber, 0.7f));
         c.segment(a.x, a.y, dragTo_.x, dragTo_.y, style.thickness * 0.7f, true);
      }
      if (hoverNode_ >= 0) {
         const Point p = box().node(frame_.orthography, hoverNode_);
         const float d = style.thickness * 4.0f;
         c.setColor(withAlpha(kEmber, 0.6f));
         c.ring(p.x - d * 0.5f, p.y - d * 0.5f, d, 1.2f);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (e.shouldTriggerPopup()) {
         wipe(e.position.x, e.position.y);
         return;
      }
      dragFrom_ = nodeNear(e.position.x, e.position.y);
      dragTo_ = {e.position.x, e.position.y};
      redraw();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      dragTo_ = {e.position.x, e.position.y};
      hoverNode_ = nodeNear(e.position.x, e.position.y);
      redraw();
   }
   void mouseUp(const visage::MouseEvent &e) override {
      const int to = nodeNear(e.position.x, e.position.y);
      if (dragFrom_ >= 0 && to >= 0 && to != dragFrom_) {
         Glyph g = standing();
         if (g.count < kMaxStrokes) {
            g.strokes[g.count++] = {static_cast<uint8_t>(dragFrom_), static_cast<uint8_t>(to)};
            if (onCarve)
               onCarve(g);
         }
      }
      dragFrom_ = -1;
      redraw();
   }
   void mouseMove(const visage::MouseEvent &e) override {
      const int n = nodeNear(e.position.x, e.position.y);
      if (n != hoverNode_) {
         hoverNode_ = n;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hoverNode_ = -1;
      redraw();
   }

private:
   GlyphBox box() const { return GlyphBox::fit(34, 10, width() - 44, height() - 20); }

   // The glyph as it stands: eroded strokes are gone for good once it is carved.
   Glyph standing() const {
      const Cell &x = frame_.cells[cell];
      Glyph g = x.glyph;
      g.count = static_cast<uint8_t>(x.visible());
      return g;
   }

   int nodeNear(float px, float py) const {
      const GlyphBox b = box();
      int best = -1;
      float bestD = b.unit * 0.35f;
      for (int n = 0; n < kNodes; ++n) {
         const Point p = b.node(frame_.orthography, n);
         const float d = std::hypot(p.x - px, p.y - py);
         if (d < bestD) {
            best = n;
            bestD = d;
         }
      }
      return best;
   }

   // Wipes the stroke nearest the point, or the dot or bar when one is nearer.
   void wipe(float px, float py) {
      Glyph g = standing();
      const GlyphBox b = box();
      int best = -1;
      float bestD = b.unit * 0.3f;
      for (int i = 0; i < g.count; ++i) {
         const Point a = b.node(frame_.orthography, g.strokes[i].from), z = b.node(frame_.orthography, g.strokes[i].to);
         const float dx = z.x - a.x, dy = z.y - a.y, len2 = std::max(1e-3f, dx * dx + dy * dy);
         const float t = std::clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, 0.0f, 1.0f);
         const float d = std::hypot(a.x + t * dx - px, a.y + t * dy - py);
         if (d < bestD) {
            best = i;
            bestD = d;
         }
      }
      if (best >= 0) {
         for (int i = best; i + 1 < g.count; ++i)
            g.strokes[i] = g.strokes[i + 1];
         --g.count;
      }
      else if (g.dot >= 0 && std::hypot(b.node(frame_.orthography, g.dot).x - px, b.node(frame_.orthography, g.dot).y - py) < b.unit * 0.3f)
         g.dot = -1;
      else if (g.bar && std::fabs(py - b.at({0, -0.55f}).y) < b.unit * 0.2f)
         g.bar = false;
      else
         return;
      if (onCarve)
         onCarve(g);
   }

   const SlabFrame &frame_;
   int dragFrom_ = -1, hoverNode_ = -1;
   Point dragTo_;
};

// What the decoded glyph plays, as a list: one line per note.
class Transcript : public visage::Frame {
public:
   explicit Transcript(const SlabFrame &frame) : frame_(frame) { setIgnoresMouseEvents(true, false); }
   int cell = 0;

   void draw(visage::Canvas &c) override {
      const float w = width();
      const Cell &x = frame_.cells[cell];
      char line[96];
      c.setColor(kDim);
      std::snprintf(line, sizeof(line), "CELL %02d", cell + 1);
      c.text(line, ui::font(12), visage::Font::kLeft, 0, 0, w, 18);
      std::string info = x.word >= 0 ? "word " + std::to_string(x.word + 1) : std::string("carved by hand");
      if (x.eroded)
         info += ", " + std::to_string(std::min<int>(x.eroded, x.glyph.count - 1)) + " worn away";
      c.text(info, ui::font(12), visage::Font::kRight, 0, 0, w, 18);
      c.setColor(kEdge);
      c.fill(0, 22, w, 1);

      const GlyphNotes notes = decode(x.glyph, x.visible());
      float y = 30;
      c.setColor(kDim);
      c.text("#", ui::font(11), visage::Font::kLeft, 0, y, 30, 16);
      c.text("NOTE", ui::font(11), visage::Font::kLeft, 30, y, 60, 16);
      c.text("STEP", ui::font(11), visage::Font::kLeft, 96, y, 60, 16);
      c.text("LEN", ui::font(11), visage::Font::kLeft, 150, y, 60, 16);
      c.text("VEL", ui::font(11), visage::Font::kLeft, 200, y, 60, 16);
      y += 20;
      for (int i = 0; i < notes.count; ++i) {
         const GlyphNote &n = notes.notes[i];
         const bool lit = frame_.playing && cell == frame_.current && frame_.step >= n.start &&
                          frame_.step < n.start + n.length;
         c.setColor(lit ? kEmber : n.ghost ? kDim : kText);
         c.text(n.ghost ? std::string("dot") : std::to_string(n.stroke + 1), ui::font(13), visage::Font::kLeft, 0, y, 30, 18);
         c.text(ui::noteName(frame_.rowPitch[n.row]), ui::font(13), visage::Font::kLeft, 30, y, 60, 18);
         c.text(std::to_string(n.start + 1), ui::font(13), visage::Font::kLeft, 96, y, 60, 18);
         c.text(std::to_string(n.length), ui::font(13), visage::Font::kLeft, 150, y, 60, 18);
         c.text(std::to_string(n.velocity), ui::font(13), visage::Font::kLeft, 200, y, 60, 18);
         y += 21;
      }
      if (notes.count == 0) {
         c.setColor(kDim);
         c.text("blank stone", ui::font(13), visage::Font::kLeft, 0, y, w, 18);
      }
      if (x.glyph.bar && notes.count) {
         c.setColor(kDim);
         c.text("overbar: last note holds", ui::font(11), visage::Font::kLeft, 0, y + 4, w, 16);
      }
   }

private:
   const SlabFrame &frame_;
};

// Caps set wide, as the slab's labels are cut: "DECODE" becomes "D E C O D E".
std::string spaced(const std::string &text) {
   std::string out;
   for (char ch : text) {
      if (!out.empty())
         out += ' ';
      out += ch;
   }
   return out;
}

// A label over a control in the left strip.
class Caption : public visage::Frame {
public:
   explicit Caption(std::string text) : text_(std::move(text)) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      c.setColor(kDim);
      c.text(spaced(text_), ui::font(height() * 0.7f), visage::Font::kLeft, 0, 0, width(), height());
   }

private:
   std::string text_;
};

// A slab of stone behind a group of controls, its name cut small into the top-left corner.
class Plate : public visage::Frame {
public:
   explicit Plate(std::string title) : title_(std::move(title)) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(kStone);
      c.roundedRectangle(0, 0, width(), height(), 6 * s);
      c.setColor(kEdge);
      c.roundedRectangleBorder(0, 0, width(), height(), 6 * s, 1.0f);
      if (title_.empty())
         return;
      c.setColor(kEmber);
      c.fill(10 * s, 9 * s, 3 * s, 10 * s);
      c.setColor(kDim);
      c.text(spaced(title_), ui::font(10 * s), visage::Font::kLeft, 18 * s, 6 * s, width(), 16 * s);
   }

private:
   std::string title_;
};

// ---- the stage ----

// What the panel becomes in LISTEN: no controls, just the stone listening, made to be captured for a
// stream. The glyph being carved stands large in the middle, a stroke cut as each note is played,
// over the faint glyph it replaces. Beside it the slab stands as it does on the panel, read from the
// top right down each column, and under that the notes of the window so far as trails of embers,
// with the last note heard. Everything moves with the song's steps, not the clock, so the same frame
// always draws the same.
class StageView : public visage::Frame {
public:
   explicit StageView(const SlabFrame &frame) : frame_(frame), tablet_(frame) {
      tablet_.setIgnoresMouseEvents(true, false);
      addChild(&tablet_);
   }

   void resized() override {
      const float s = width() / kDesignWidth;
      tablet_.setBounds(kTablet.x * s, kTablet.y * s, kTablet.w * s, kTablet.h * s);
   }

   // Redraws the stage and the slab on it, for a new frame.
   void refresh() {
      redraw();
      tablet_.redraw();
   }

   // Back to SPEAK, from the faint button in the corner; and the background menu (UI size), for
   // fitting the window to a capture.
   std::function<void()> onSpeak;
   std::function<void(visage::Point)> onMenu;

   void draw(visage::Canvas &c) override {
      const float s = width() / kDesignWidth;
      const float step = frame_.playing ? frame_.step : 0.0f;
      const int heard = std::clamp(frame_.heard, 0, kMaxHeard);

      // How hot the stone is: the newest notes flare and cool over a few steps.
      float heat = 0.0f;
      int newest = -1;
      for (int i = 0; i < heard; ++i) {
         const HeardNote &n = frame_.heardNotes[i];
         const float age = std::max(0.0f, step - n.start);
         heat = std::max(heat, n.velocity / 127.0f * std::exp(-age * 0.45f));
         if (newest < 0 || n.start >= frame_.heardNotes[newest].start)
            newest = i;
      }

      c.setColor(kBackground);
      c.fill(0, 0, width(), height());
      // Faint strata across the stone.
      for (int k = 1; k < 14; ++k) {
         c.setColor(withAlpha(0xffffffff, k % 3 ? 0.012f : 0.022f));
         c.fill(0, k * 48 * s, width(), 1);
      }
      // The glow behind the glyph, breathing with what's played: bright in the middle, falling off
      // smoothly to nothing.
      const float gx = 440 * s, gy = 330 * s, r = 400 * s * (1.0f + 0.05f * heat);
      const float a = 0.16f + 0.2f * heat;
      const visage::Gradient glow = visage::Gradient::fromSampleFunction(64, [a](float t) {
         const float k = (1.0f - t) * (1.0f - t);
         return visage::Color(withAlpha(kEmber, a * k * k + a * 0.6f * k));
      });
      c.setColor(visage::Brush::radial(glow, {gx, gy}, r));
      c.fill(gx - r, gy - r, 2 * r, 2 * r);

      drawHeader(c, s, step);
      drawGlyphStage(c, s, step, heat, heard, newest);
      drawWindow(c, s, step, heard);
      drawTrails(c, s, step, heard, newest);

      // The way back, barely there until the mouse finds it.
      c.setColor(withAlpha(kText, hover_ ? 0.85f : 0.16f));
      c.text(spaced("SPEAK"), ui::font(11 * s), visage::Font::kRight, 1040 * s, 640 * s, 140 * s, 24 * s);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (e.shouldTriggerPopup()) {
         if (onMenu)
            onMenu(e.position);
      }
      else if (overSpeak(e.position) && onSpeak)
         onSpeak();
   }
   void mouseMove(const visage::MouseEvent &e) override {
      if (overSpeak(e.position) != hover_) {
         hover_ = !hover_;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }

private:
   // The slab stands on the right, as on the panel and in its shape; the window's notes run under it.
   static constexpr ui::Rect kTablet{840, 72, 260, 456}, kRoll{840, 566, 260, 58};

   bool overSpeak(visage::Point p) const {
      const float s = width() / kDesignWidth;
      return p.x > 1080 * s && p.y > 630 * s;
   }

   // The name in the top left; LISTENING with a light that beats in the top right.
   void drawHeader(visage::Canvas &c, float s, float step) const {
      c.setColor(kEmber);
      c.fill(40 * s, 34 * s, 6 * s, 26 * s);
      c.setColor(withAlpha(kEmber, 0.6f));
      c.fill(50 * s, 40 * s, 6 * s, 20 * s);
      c.setColor(withAlpha(kEmber, 0.3f));
      c.fill(60 * s, 46 * s, 6 * s, 14 * s);
      c.setColor(kText);
      c.text(spaced("MONOLITH"), ui::font(22 * s), visage::Font::kLeft, 78 * s, 26 * s, 300 * s, 30 * s);
      c.setColor(kDim);
      std::string script = kOrthographyNames[static_cast<int>(frame_.orthography)];
      std::transform(script.begin(), script.end(), script.begin(), [](unsigned char ch) { return std::toupper(ch); });
      c.text(spaced(script + " SCRIPT"), ui::font(9 * s), visage::Font::kLeft, 79 * s, 52 * s, 300 * s, 14 * s);

      // One beat in four steps: the light is brightest on the beat.
      const float beat = step - 4.0f * std::floor(step / 4.0f);
      const float pulse = frame_.playing ? 0.35f + 0.65f * std::exp(-beat * 1.2f) : 0.3f;
      const float lx = 1160 * s, ly = 46 * s, d = 12 * s;
      c.setColor(withAlpha(kEmber, 0.25f * pulse));
      c.circle(lx - d * 1.5f, ly - d * 1.5f, d * 3.0f);
      c.setColor(withAlpha(kEmber, pulse));
      c.circle(lx - d * 0.5f, ly - d * 0.5f, d);
      c.setColor(frame_.playing ? kText : kDim);
      c.text(spaced(frame_.playing ? "LISTENING" : "WAITING"), ui::font(13 * s), visage::Font::kRight, 850 * s,
             36 * s, 280 * s, 20 * s);
   }

   // The cell's glyph large on its nodes: the one it holds faint, the one being carved burning over
   // it, its newest stroke hottest.
   void drawGlyphStage(visage::Canvas &c, float s, float step, float heat, int heard, int newest) const {
      const Cell &cell = frame_.cells[std::clamp(frame_.current, 0, kCells - 1)];
      const GlyphBox box = GlyphBox::fit(240 * s, 84 * s, 400 * s, 500 * s);
      const float t = 4.5f * s;
      GlyphStyle ghost;
      ghost.thickness = t;
      ghost.nodes = true;
      ghost.color = withAlpha(kText, heard ? 0.07f : 0.16f);
      drawGlyph(c, cell.glyph, cell.visible(), frame_.orthography, box, ghost);
      // Each row's note beside it.
      for (int r = 0; r < kGridRows; ++r) {
         const Point p = box.node(frame_.orthography, nodeAt(0, r));
         c.setColor(withAlpha(kDim, 0.8f));
         c.text(ui::noteName(frame_.rowPitch[r]), ui::font(13 * s), visage::Font::kRight, p.x - 96 * s, p.y - 9 * s,
                60 * s, 18 * s);
      }
      const Glyph &g = frame_.carving;
      if (!heard || g.count == 0)
         return;
      GlyphStyle burning;
      burning.thickness = t;
      burning.glow = 0.6f;
      drawGlyph(c, g, g.count, frame_.orthography, box, burning);
      // The newest stroke flares, with a spark where the pen is.
      const Stroke last = g.strokes[g.count - 1];
      drawLitStroke(c, box, frame_.orthography, last, t, kEmber, 0.6f + 0.4f * heat);
      if (newest >= 0) {
         const HeardNote &n = frame_.heardNotes[newest];
         const bool held = n.end < 0.0f;
         const float along = held ? std::clamp((step - n.start) / 4.0f, 0.15f, 1.0f) : 1.0f;
         const Point p = strokePoint(box, frame_.orthography, last, along);
         const float d = t * (2.0f + 2.5f * heat);
         c.setColor(withAlpha(kEmber, 0.3f + 0.3f * heat));
         c.circle(p.x - d, p.y - d, d * 2.0f);
         c.setColor(0xffffffffu);
         c.circle(p.x - d * 0.35f, p.y - d * 0.35f, d * 0.7f);
      }
   }

   // The window's sixteen steps under the glyph: those gone by lit, a mark where each note came in.
   void drawWindow(visage::Canvas &c, float s, float step, int heard) const {
      const float x0 = 250 * s, w = 380 * s, y = 600 * s, cellW = w / kWindowSteps;
      for (int k = 0; k < kWindowSteps; ++k) {
         const bool past = frame_.playing && k + 1 <= step;
         const bool now = frame_.playing && k <= step && step < k + 1;
         c.setColor(now ? kPeach : past ? withAlpha(kEmber, k % 4 ? 0.55f : 0.85f) : withAlpha(kEdge, k % 4 ? 0.8f : 1.0f));
         c.fill(x0 + k * cellW + 2 * s, y, cellW - 4 * s, (k % 4 ? 4.0f : 6.0f) * s);
      }
      for (int i = 0; i < heard; ++i) {
         const HeardNote &n = frame_.heardNotes[i];
         const float x = x0 + std::clamp(n.start, 0.0f, 16.0f) * cellW;
         const float d = (4.0f + 4.0f * n.velocity / 127.0f) * s;
         c.setColor(withAlpha(kPeach, 0.9f));
         c.circle(x - d * 0.5f, y - 10 * s - d * 0.5f, d);
      }
      char line[48];
      std::snprintf(line, sizeof(line), "CELL %02d", std::clamp(frame_.current, 0, kCells - 1) + 1);
      c.setColor(kDim);
      c.text(spaced(line), ui::font(10 * s), visage::Font::kLeft, x0, y + 14 * s, w, 16 * s);
      std::snprintf(line, sizeof(line), "%d HEARD", heard);
      c.text(spaced(line), ui::font(10 * s), visage::Font::kRight, x0, y + 14 * s, w, 16 * s);
   }

   // The window's notes as trails of embers: across in steps, up in pitch.
   // Under the slab, with the last note heard beside its title.
   void drawTrails(visage::Canvas &c, float s, float step, int heard, int newest) const {
      const float x = kRoll.x * s, y = kRoll.y * s, w = kRoll.w * s, h = kRoll.h * s;
      c.setColor(kDim);
      c.text(spaced("THIS GLYPH"), ui::font(10 * s), visage::Font::kLeft, x, y - 22 * s, w, 16 * s);
      if (newest >= 0) {
         const HeardNote &n = frame_.heardNotes[newest];
         const float fresh = std::exp(-std::max(0.0f, step - n.start) * 0.35f);
         c.setColor(fresh > 0.5f ? kPeach : withAlpha(kText, 0.55f + 0.9f * fresh));
         c.text(ui::noteName(n.pitch), ui::font(14 * s), visage::Font::kRight, x, y - 25 * s, w, 20 * s);
      }
      c.setColor(withAlpha(kEdge, 0.6f));
      for (int k = 0; k <= kWindowSteps; k += 4)
         c.fill(x + w * k / kWindowSteps, y, 1, h);
      int low = *std::min_element(frame_.rowPitch.begin(), frame_.rowPitch.end()) - 2;
      int high = *std::max_element(frame_.rowPitch.begin(), frame_.rowPitch.end()) + 2;
      for (int i = 0; i < heard; ++i) {
         low = std::min<int>(low, frame_.heardNotes[i].pitch - 2);
         high = std::max<int>(high, frame_.heardNotes[i].pitch + 2);
      }
      const auto yOf = [&](int pitch) { return y + h - (pitch - low) / static_cast<float>(std::max(1, high - low)) * h; };
      for (int i = 0; i < heard; ++i) {
         const HeardNote &n = frame_.heardNotes[i];
         const bool held = n.end < 0.0f;
         const float end = std::clamp(held ? std::max(step, n.start + 0.25f) : n.end, 0.0f, 16.0f);
         const float x0 = x + w * std::clamp(n.start, 0.0f, 16.0f) / kWindowSteps, x1 = x + w * end / kWindowSteps;
         const float py = yOf(n.pitch), t = (2.0f + 3.0f * n.velocity / 127.0f) * s;
         const float fresh = std::exp(-std::max(0.0f, step - n.start) * 0.35f);
         c.setColor(withAlpha(kEmber, 0.15f + 0.15f * fresh));
         c.roundedRectangle(x0 - t, py - t * 1.5f, std::max(t * 2, x1 - x0 + 2 * t), t * 3, t * 1.5f);
         c.setColor(withAlpha(held ? kPeach : kEmber, 0.6f + 0.4f * fresh));
         c.roundedRectangle(x0, py - t * 0.5f, std::max(t, x1 - x0), t, t * 0.5f);
         if (held) {
            c.setColor(0xffffffffu);
            c.circle(x1 - t * 0.6f, py - t * 0.6f, t * 1.2f);
         }
      }
      if (frame_.playing) {
         c.setColor(withAlpha(kPeach, 0.5f));
         c.fill(x + w * std::clamp(step, 0.0f, 16.0f) / kWindowSteps, y - 4 * s, 1.5f * s, h + 8 * s);
      }
   }

   const SlabFrame &frame_;
   SlabView tablet_;
   bool hover_ = false;
};

// ---- the panel ----

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      frame_ = host_.frame();
      tips_.setTheme(emberTheme());
      buildHeader();
      buildLeft();
      buildSlab();
      buildDecode();
      buildKnobs();
      buildStage();
      refresh();
      startTimer(30);
   }

   void draw(visage::Canvas &c) override {
      const float s = scale();
      c.setColor(kBackground);
      c.fill(0, 0, width(), height());
      // The name, cut into the stone: a monolith of three bars, then the word.
      c.setColor(kEmber);
      c.fill(20 * s, 16 * s, 6 * s, 26 * s);
      c.setColor(withAlpha(kEmber, 0.6f));
      c.fill(30 * s, 22 * s, 6 * s, 20 * s);
      c.setColor(withAlpha(kEmber, 0.3f));
      c.fill(40 * s, 28 * s, 6 * s, 14 * s);
      c.setColor(kText);
      c.text(spaced("MONOLITH"), ui::font(22 * s), visage::Font::kLeft, 58 * s, 8 * s, 260 * s, 30 * s);
      c.setColor(kDim);
      c.text(spaced("GLYPH NOTE GENERATOR"), ui::font(9 * s), visage::Font::kLeft, 59 * s, 34 * s, 300 * s, 14 * s);
      c.setColor(kEdge);
      c.fill(16 * s, 56 * s, (kDesignWidth - 32) * s, 1 * s);
   }

   void timerCallback() override { refresh(); }

   // Pulls the newest frame and redraws what shows it.
   void refresh() {
      frame_ = host_.frame();
      // LISTEN turns the panel into the stage; nothing under it needs drawing meanwhile.
      if (frame_.listening != stage_->isVisible())
         stage_->setVisible(frame_.listening);
      if (frame_.listening) {
         stage_->refresh();
         return;
      }
      const int shown = selected_ >= 0 ? selected_ : frame_.current;
      slab_->selected = selected_;
      decode_->cell = shown;
      transcript_->cell = shown;
      slab_->redraw();
      decode_->redraw();
      transcript_->redraw();
      updateRoll();
      std::string status;
      if (frame_.listening)
         status = frame_.playing ? "LISTENING: " + std::to_string(frame_.heard) + " heard" : "LISTENING: waiting for play";
      else
         status = frame_.playing ? "READING" : "AT REST";
      if (status != status_) {
         status_ = status;
         statusLabel_->setText(status);
      }
   }

   void select(int cell) { selected_ = cell; }

private:
   template <typename T>
   T &themed(T &widget) {
      widget.setTheme(emberTheme());
      return widget;
   }

   ui::Knob &knob(clap_id id, const char *label, ui::Rect cell, const char *tip, std::vector<double> stops = {}) {
      ui::ValueRange range = rangeOf(id);
      range.stops = std::move(stops);
      auto &k = themed(add<ui::Knob>(cell, range));
      k.setLabel(label);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      tips_.add(k, tip);
      return k;
   }

   void caption(ui::Rect r, const char *t) { add<Caption>(r, t); }

   void buildHeader() {
      auto &update = add<ui::UpdateNotice>({228, 15, 162, 26});
      update.setTheme(emberTheme());
      update.setAccent(kEmber);
      auto &presets = add<ui::PresetBar>({400, 14, 380, 28}, factoryPresets(), kPresetFormat, host_);
      presets.setTheme(emberTheme());
      presets.defaults = defaultValues();
      presets.currentName = [this] { return host_.presetName(); };
      presets.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };

      caption({792, 21, 44, 15}, "SEED");
      auto &seed = themed(add<ui::NumberBox>({838, 14, 110, 28}, rangeOf(kSeed, 1.0)));
      seed.setStepButtons(true);
      seed.valueText = [](double v) { return text(kSeed, v); };
      bindings_.bind(seed, kSeed);
      tips_.add(seed, "The seed the scribe writes from. The same settings and seed always give the same slab.");
      auto &reroll = themed(add<ui::Button>({954, 14, 72, 28}, "REROLL"));
      reroll.onClick = [this] {
         const auto now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
         host_.beginEdit(kSeed);
         host_.setParam(kSeed, static_cast<double>(hashMix(now, 77) % 10000));
         host_.endEdit(kSeed);
      };
      tips_.add(reroll, "A new seed: the scribe writes a whole new slab.");
      auto &lock = themed(add<ui::Button>({1032, 14, 64, 28}, "LOCK"));
      bindings_.bind(lock, kLock);
      tips_.add(lock, "Hold the slab still: no drift, no erosion. A short length then loops exactly.");
      auto &restore = themed(add<ui::Button>({1102, 14, 82, 28}, "RESTORE"));
      restore.onClick = [this] {
         Command c;
         c.kind = Command::Kind::Restore;
         host_.command(c);
      };
      tips_.add(restore, "Bring back every stroke erosion has worn away.");
   }

   void buildLeft() {
      add<Plate>({16, 70, 180, 594}, "DIALECT");
      float y = 96;
      const auto dropdown = [&](clap_id id, const char *label, const char *tip) {
         caption({28, y, 156, 14}, label);
         auto &d = themed(add<ui::Dropdown>({28, y + 16, 156, 26}, namesOf(id)));
         bindings_.bind(d, id);
         tips_.add(d, tip);
         y += 52;
      };
      dropdown(kOrthography, "SCRIPT",
               "The orthography the scribe writes in. Each draws its strokes its own way and allows its own "
               "shapes, so each has its own feel; a stroke plays the same in any of them.");
      dropdown(kRoot, "ROOT", "The key's root.");
      dropdown(kScale, "SCALE", "The scale the eight glyph rows are taken from.");
      dropdown(kSpread, "SPREAD",
               "How far apart the eight rows are in the scale, with the root on the third row up. STEPS "
               "spans about an octave, THIRDS two, FOURTHS three and FIFTHS four.");
      caption({28, y, 156, 14}, "OCTAVE");
      auto &octave = themed(add<ui::NumberBox>({28, y + 16, 156, 26}, rangeOf(kOctave, 1.0)));
      octave.setStepButtons(true);
      octave.valueText = [](double v) { return text(kOctave, v); };
      bindings_.bind(octave, kOctave);
      tips_.add(octave, "The octave the eight rows start in.");
      y += 52;
      caption({28, y, 72, 14}, "RANGE");
      auto &range = themed(add<ui::NumberBox>({28, y + 16, 72, 26}, rangeOf(kRange, 1.0)));
      range.setStepButtons(true);
      range.valueText = [](double v) { return std::to_string(std::lround(v)); };
      bindings_.bind(range, kRange);
      tips_.add(range, "How many octaves (1 to 4) the phrase moves through, upward from OCTAVE. Each note takes the octave "
                       "nearest the last one, so the line changes octave where the leap is shortest.");
      caption({108, y, 76, 14}, "PATH");
      auto &path = themed(add<ui::Dropdown>({108, y + 16, 76, 26}, namesOf(kPath)));
      bindings_.bind(path, kPath);
      tips_.add(path, "How the phrase moves through RANGE. LOOP goes up an octave each time round and back down, "
                      "CLIMB wanders up and down a glyph at a time, CONTOUR arches over the glyphs read.");
      y += 52;
      dropdown(kRate, "RATE", "How long one step is. A glyph is sixteen steps.");
      dropdown(kDirection, "DIRECTION", "The order the slab's cells are read in.");
      caption({28, y, 156, 14}, "MODE");
      auto &mode = themed(add<ui::SegmentedSwitch>({28, y + 16, 156, 26}, std::vector<std::string>{"SPEAK", "LISTEN"}));
      bindings_.bind(mode, kMode);
      tips_.add(mode, "SPEAK reads the slab out as notes. LISTEN plays nothing: what you play is carved into the "
                      "cell being read, a glyph per sixteen steps, and the panel becomes a stage for streaming.");
      y += 52;
      caption({28, y, 156, 14}, "MIDI CHANNEL");
      auto &channel = themed(add<ui::NumberBox>({28, y + 16, 156, 26}, rangeOf(kChannel, 1.0)));
      channel.setStepButtons(true);
      channel.valueText = [](double v) { return text(kChannel, v); };
      bindings_.bind(channel, kChannel);
      y += 52;
      auto &follow = themed(add<ui::Button>({28, y + 6, 156, 26}, "KEY FOLLOW"));
      bindings_.bind(follow, kFollow);
      tips_.add(follow, "While you hold a note, it becomes the root. Held notes are not passed on.");
   }

   void buildSlab() {
      slab_ = &add<SlabView>({208, 66, 330, 604}, frame_);
      slab_->onSelect = [this](int i) { selected_ = selected_ == i ? -1 : i; refresh(); };
      slab_->onJump = [this](int i) { jump(i); };
      // A click starts reading at the glyph; a drag also sets the length to the glyphs it spans. The
      // voice reads the first of them next either way.
      slab_->onBeginSpan = [this](int first, int) {
         host_.beginEdit(kStart);
         host_.setParam(kStart, first + 1);
      };
      slab_->onSpan = [this](int first, int count) {
         if (!spanning_) {
            spanning_ = true;
            host_.beginEdit(kLength);
         }
         host_.setParam(kStart, first + 1);
         host_.setParam(kLength, count);
      };
      slab_->onEndSpan = [this](int first, int) {
         host_.endEdit(kStart);
         if (spanning_)
            host_.endEdit(kLength);
         spanning_ = false;
         jump(first);
      };
      slab_->onReroll = [this](int i) {
         Command c;
         c.kind = Command::Kind::Reroll;
         c.cell = static_cast<int8_t>(i);
         host_.command(c);
      };
      tips_.add(*slab_, "The slab, read from the top right down each column. Click a glyph to start reading "
                        "there and decode it; drag across glyphs to read just those. Alt-click to read one next "
                        "without moving the start, shift-click to have the scribe write it again.");
   }

   void jump(int cell) {
      Command c;
      c.kind = Command::Kind::Jump;
      c.cell = static_cast<int8_t>(cell);
      host_.command(c);
   }

   void buildDecode() {
      add<Plate>({550, 70, 634, 250}, "DECODE");
      decode_ = &add<DecodeView>({558, 94, 200, 218}, frame_);
      decode_->onCarve = [this](const Glyph &g) {
         Command c;
         c.kind = Command::Kind::Set;
         c.cell = static_cast<int8_t>(decode_->cell);
         c.glyph = g;
         host_.command(c);
      };
      tips_.add(*decode_, "Drag from node to node to carve a stroke; right-click a stroke to wipe it. A stroke's "
                          "start row is its note, its start column how hard it plays, its length how long.");
      transcript_ = &add<Transcript>({776, 94, 394, 196}, frame_);
      statusLabel_ = &add<ui::Label>({776, 292, 394, 20}, "", kEmber, visage::Font::kLeft, 0.62f);

      add<Plate>({550, 332, 634, 104}, "PHRASE");
      roll_ = &themed(add<ui::NoteRoll>({558, 356, 618, 72}));
      roll_->setSteps(4 * kWindowSteps, kWindowSteps);
   }

   void buildKnobs() {
      add<Plate>({550, 448, 311, 108}, "SCRIBE");
      add<Plate>({873, 448, 311, 108}, "SLAB");
      add<Plate>({550, 564, 634, 100}, "OUTPUT");
      std::vector<ui::Rect> cells = ui::row({556, 468, 299, 84}, 3);
      for (const ui::Rect &r : ui::row({879, 468, 299, 84}, 3))
         cells.push_back(r);
      for (const ui::Rect &r : ui::row({620, 582, 500, 78}, 4))
         cells.push_back(r);
      knob(kDensity, "DENSITY", cells[0], "Strokes per glyph, so notes per glyph: from one long note to six quick ones.");
      knob(kLexicon, "LEXICON", cells[1], "How many different words the scribe knows. Few: a phrase that repeats "
                                          "itself; many: one that keeps saying something new.");
      knob(kGrammar, "GRAMMAR", cells[2], "How freely words follow one another. Low keeps to habits; high "
                                          "wanders and bends the words themselves.");
      knob(kLength, "LENGTH", cells[3], "How many glyphs are read before the voice goes round again: one bar to the "
                                        "whole slab at 1/16.", {1, 2, 4, 8, 16, 32, 55});
      knob(kDrift, "DRIFT", cells[4], "The chance the scribe rewrites a glyph after it's read.");
      knob(kErode, "ERODE", cells[5], "The chance a glyph loses a stroke after it's read, thinning the phrase out.");
      knob(kSwing, "SWING", cells[6], "Delays every second step.");
      knob(kVelocity, "VELOCITY", cells[7], "How hard the notes play overall; the glyphs' columns still accent them.");
      knob(kGate, "GATE", cells[8], "Note length, as a share of the stroke's.");
      knob(kHumanize, "HUMANIZE", cells[9], "Small pushes in timing and velocity, the same each time round.");
   }

   void buildStage() {
      stage_ = &add<StageView>({0, 0, kDesignWidth, kDesignHeight}, frame_);
      stage_->setVisible(false);
      stage_->onSpeak = [this] {
         host_.beginEdit(kMode);
         host_.setParam(kMode, static_cast<double>(Mode::Speak));
         host_.endEdit(kMode);
      };
      stage_->onMenu = [this](visage::Point p) { showZoomMenu(*stage_, p); };
   }

   // The phrase: the glyph being read and the three after it in the slab, in reading order.
   void updateRoll() {
      std::vector<ui::NoteRoll::Note> notes;
      const int length = std::max(1, frame_.length);
      for (int k = 0; k < 4; ++k) {
         const int cell = windowStep(frame_.current, k, frame_.start, length);
         const Cell &x = frame_.cells[cell];
         const GlyphNotes decoded = decode(x.glyph, x.visible());
         for (int i = 0; i < decoded.count; ++i) {
            const GlyphNote &n = decoded.notes[i];
            notes.push_back({static_cast<float>(k * kWindowSteps + n.start), static_cast<float>(n.length),
                             frame_.rowPitch[n.row], n.velocity / 127.0f});
         }
      }
      roll_->setLanes(std::vector<int>(frame_.rowPitch.begin(), frame_.rowPitch.end()));
      roll_->setNotes(notes);
      roll_->setPlayhead(frame_.playing ? frame_.step : -1.0f);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   SlabFrame frame_;
   int selected_ = -1;
   bool spanning_ = false;
   SlabView *slab_ = nullptr;
   DecodeView *decode_ = nullptr;
   Transcript *transcript_ = nullptr;
   ui::NoteRoll *roll_ = nullptr;
   StageView *stage_ = nullptr;
   ui::Label *statusLabel_ = nullptr;
   std::string status_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, int selected, const char *pngPath) {
   Panel panel(host);
   panel.stopTimer();
   panel.select(selected);
   panel.refresh();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace monolith
