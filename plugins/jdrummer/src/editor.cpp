// JDrummer's panel, on a 1000 x 640 design grid scaled to the window, in JKit's style: a title bar
// with the three tabs, a small transport and the preset browser, and cards under it.
//
//    GENERATE   GENRE (twelve tiles, the style, the browser of all 34), HOW IT PLAYS (the drummer and
//               a percussionist, ENERGY, WEIRD, ROLL SONG), THE SONG (seven sections, each its own
//               dice, lock and energy), MUTATE (the section now and five children to pick from)
//    EDIT       one section's variation, fill or percussion part: only the pads it plays, each hit's
//               velocity, chance, ratchet and nudge
//    PLAY       SECTIONS (picked by click, note or param), NOW PLAYING, LIFE, TRANSPORT, OUTPUT
//
// Colour carries meaning: each pad's role has its colour (kick orange, snare and clap blue, hats
// purple, toms and percussion green, effects yellow); orange is also what is selected and playing.

#include "editor.h"
#include "engine.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/presets.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <visage/widgets.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "kit.h"
#include "params.h"
#include "players.h"
#include "presets.h"
#include "styles.h"

namespace jdrummer {
namespace {

namespace ui = jclap::ui;
using PlayFrame = jdrummer::Frame;

constexpr float kW = kEditorWidth, kH = kEditorHeight;
constexpr float kHeader = 64.0f;

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff8fd1a8;
constexpr unsigned kYellow = 0xffe8c66a;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kSlate = 0xff454c58;
constexpr unsigned kRule = 0xff2c323b;
constexpr unsigned kCell = 0xff272c34;
constexpr unsigned kEmpty = 0xff1f232a;
constexpr unsigned kWell = 0xff181b20;
constexpr unsigned kTitle = 0xffc3c7cd;
constexpr unsigned kFaint = 0xff5d636c;
constexpr unsigned kWarm = 0xff2c2620; // a selected card's fill

enum Page { kGeneratePage, kEditPage, kPlayPage };

unsigned alpha(unsigned c, float a) {
   return (static_cast<unsigned>(std::clamp(a, 0.0f, 1.0f) * ((c >> 24) & 0xff)) << 24) | (c & 0xffffff);
}

unsigned roleColour(int kit, int pad) {
   switch (padRole(kit, pad)) {
   case kKick: return kOrange;
   case kSnare: return kBlue;
   case kHat: return kPurple;
   case kFx: return kYellow;
   default: return kGreen;
   }
}

std::string upper(std::string s) {
   for (char &c : s)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
   return s;
}

std::string pct(double v) { return std::to_string(static_cast<int>(std::lround(v * 100.0))); }

// The kit a pattern's pads are named and coloured by: the percussion part is the percussionist's
// kit (Afro-Latin or Percussion); the rest follow KIT.
int kitOf(const EditorHost &h, bool perc) {
   if (perc)
      return percussionistKit(std::max(0, static_cast<int>(std::lround(h.paramValue(kPercussionist))) - 1));
   return std::clamp(static_cast<int>(std::lround(h.paramValue(kKit))), 0, kKitCount - 1);
}

void drawCard(visage::Canvas &c, float s, float x, float y, float w, float h, const std::string &title,
              const std::string &note = {}, unsigned titleColour = kOrange) {
   const ui::Theme &t = ui::defaultTheme();
   c.setColor(t.panel);
   c.roundedRectangle(x * s, y * s, w * s, h * s, 6 * s);
   c.setColor(t.panelBorder);
   c.roundedRectangleBorder(x * s, y * s, w * s, h * s, 6 * s, 1.0f);
   if (!title.empty()) {
      c.setColor(titleColour);
      c.text(title, ui::font(11 * s), visage::Font::kLeft, (x + 13) * s, (y + 9) * s, (w - 26) * s, 16 * s);
   }
   if (!note.empty()) {
      c.setColor(alpha(t.dimText, 0.85f));
      c.text(note, ui::font(9 * s), visage::Font::kRight, (x + 13) * s, (y + 9) * s, (w - 26) * s, 16 * s);
   }
}

void label(visage::Canvas &c, float s, const std::string &l, float x, float y, float w = 160, unsigned colour = 0,
           visage::Font::Justification j = visage::Font::kLeft, float size = 9.5f) {
   c.setColor(colour ? colour : ui::defaultTheme().dimText);
   c.text(l, ui::font(size * s), j, x * s, y * s, w * s, (size + 5) * s);
}

// A dashed rounded-ish outline (square corners), in the current colour.
void dashed(visage::Canvas &c, float x, float y, float w, float h, float dash, float t) {
   for (float a = 0; a < w; a += dash * 2) {
      const float l = std::min(dash, w - a);
      c.fill(x + a, y, l, t);
      c.fill(x + a, y + h - t, l, t);
   }
   for (float a = 0; a < h; a += dash * 2) {
      const float l = std::min(dash, h - a);
      c.fill(x, y + a, t, l);
      c.fill(x + w - t, y + a, t, l);
   }
}

// The pads a pattern plays, top to bottom as the panel lists them (pad 16 at the top, the kick at
// the bottom).
std::vector<int> lanesOf(const Pattern &p, uint16_t extra = 0) {
   std::vector<int> lanes;
   for (int pad = kPads - 1; pad >= 0; --pad)
      if (p.uses(pad) || (extra >> pad & 1))
         lanes.push_back(pad);
   return lanes;
}

// A pattern in miniature: a row per pad it plays, a cell per step, coloured by role and as tall as
// its velocity. `playing` lights a column.
void drawThumb(visage::Canvas &c, float s, const Pattern &p, int kit, float x, float y, float w, float h,
               int playing = -1, float dim = 1.0f) {
   c.setColor(kWell);
   c.roundedRectangle(x * s, y * s, w * s, h * s, 3 * s);
   const std::vector<int> lanes = lanesOf(p);
   if (lanes.empty()) {
      c.setColor(kFaint);
      c.text("silent", ui::font(8.5f * s), visage::Font::kCenter, x * s, y * s, w * s, h * s);
      return;
   }
   const int steps = std::max<int>(1, p.steps);
   const float pad = 3, cw = (w - 2 * pad) / steps;
   const float rh = std::min(10.0f, (h - 2 * pad) / static_cast<float>(lanes.size()));
   const float top = y + (h - rh * lanes.size()) / 2;
   // Beat lines.
   c.setColor(0xff22262d);
   for (int st = 4; st < steps; st += 4)
      c.fill((x + pad + st * cw) * s, (y + 2) * s, 0.7f * s, (h - 4) * s);
   if (playing >= 0 && playing < steps) {
      c.setColor(0x22ffffff);
      c.fill((x + pad + playing * cw) * s, (y + 1) * s, cw * s, (h - 2) * s);
   }
   for (size_t li = 0; li < lanes.size(); ++li) {
      const int padIndex = lanes[li];
      const unsigned col = roleColour(kit, padIndex);
      for (int st = 0; st < steps; ++st) {
         const Hit &hit = p.at(padIndex, st);
         if (!hit.on())
            continue;
         const float vh = std::max(0.35f, hit.vel / 127.0f) * (rh - 1.2f);
         c.setColor(alpha(col, dim * (hit.chance < 100 ? 0.55f : 1.0f)));
         c.fill((x + pad + st * cw + 0.4f) * s, (top + li * rh + (rh - 1.2f - vh)) * s, std::max(0.8f, cw - 1.0f) * s,
                vh * s);
      }
   }
}

// ---- small widgets ----

class CheckBox : public visage::Frame {
public:
   explicit CheckBox(std::string label) : label_(std::move(label)) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const float box = 11, y = (height() / s - box) / 2;
      if (on) {
         c.setColor(kSlate);
         c.roundedRectangle(0, y * s, box * s, box * s, 2.5f * s);
         c.setColor(kOrange);
         c.roundedRectangle(3 * s, (y + 3) * s, (box - 6) * s, (box - 6) * s, 1.5f * s);
      }
      c.setColor(t.dimText);
      c.roundedRectangleBorder(0, y * s, box * s, box * s, 2.5f * s, 1.0f * s);
      c.setColor(on ? t.text : t.dimText);
      c.text(label_, ui::font(9.5f * s), visage::Font::kLeft, (box + 6) * s, 0, width() - (box + 6) * s, height());
   }
   void mouseDown(const visage::MouseEvent &) override {
      on = !on;
      redraw();
      if (onToggle)
         onToggle(on);
   }
   bool on = false;
   std::function<void(bool)> onToggle;

private:
   std::string label_;
};

// A button drawn by the panel's rules: plain, lit (slate), or the accent (GoButton).
class FlatButton : public visage::Frame {
public:
   explicit FlatButton(std::string text, float size = 9.5f) : text_(std::move(text)), size_(size) {}
   void setText(const std::string &t) {
      if (t != text_) {
         text_ = t;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const unsigned fill = accent ? (hover_ ? 0xfff59a3fu : kOrange) : lit ? kSlate : hover_ ? t.buttonHover : t.button;
      c.setColor(fill);
      c.roundedRectangle(0, 0, width(), height(), 5 * s);
      if (!accent) {
         c.setColor(outline ? outline : t.panelBorder);
         c.roundedRectangleBorder(0, 0, width(), height(), 5 * s, 1.0f);
      }
      float tx = 0;
      if (dice) {
         c.setColor(accent ? 0xff1b1408u : kOrange);
         const float d = std::min(height() * 0.5f, 14 * s);
         ui::drawDie(c, width() / 2 - textWidth() * s / 2 - d * 0.4f, height() / 2, d);
         tx = d * 0.55f / s;
      }
      c.setColor(accent ? 0xff1b1408u : enabled ? (textColour ? textColour : t.text) : kFaint);
      c.text(text_, ui::font(size_ * s), visage::Font::kCenter, tx * s, 0, width(), height());
   }
   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (enabled && onClick)
         onClick(e);
   }
   bool accent = false, lit = false, dice = false, enabled = true;
   unsigned outline = 0, textColour = 0;
   std::function<void(const visage::MouseEvent &)> onClick;

private:
   float textWidth() const { return text_.size() * size_ * 0.62f; }
   std::string text_;
   float size_;
   bool hover_ = false;
};

// A flat slider for a param, with words at each end: ENERGY (chill ... peak time), WEIRD.
class ParamSlider : public visage::Frame {
public:
   ParamSlider(EditorHost &host, clap_id id, std::string name, std::string left, std::string right, unsigned colour)
      : host_(host), id_(id), name_(std::move(name)), left_(std::move(left)), right_(std::move(right)),
        colour_(colour) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float w = width() / s;
      const double v = host_.paramValue(id_);
      const float f = static_cast<float>((v - kParams[id_].min) / (kParams[id_].max - kParams[id_].min));
      if (!name_.empty()) {
         label(c, s, name_, 0, 0, 120, kWhite, visage::Font::kLeft, 10.5f);
         label(c, s, pct(f), w - 60, 0, 60, colour_, visage::Font::kRight, 10.5f);
      }
      const float ty = name_.empty() ? height() / s / 2 - 2 : 21;
      c.setColor(ui::defaultTheme().track);
      c.roundedRectangle(0, ty * s, w * s, 4 * s, 2 * s);
      c.setColor(colour_);
      c.roundedRectangle(0, ty * s, std::max(4.0f, f * w) * s, 4 * s, 2 * s);
      c.setColor(kWhite);
      c.circle((f * (w - 14)) * s, (ty - 5) * s, 14 * s);
      if (!left_.empty()) {
         label(c, s, left_, 0, ty + 10, 120, 0, visage::Font::kLeft, 8.5f);
         label(c, s, right_, w - 120, ty + 10, 120, 0, visage::Font::kRight, 8.5f);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (e.repeatClickCount() >= 2) {
         host_.beginEdit(id_);
         host_.setParam(id_, kParams[id_].defaultValue);
         host_.endEdit(id_);
         redraw();
         if (onDone)
            onDone();
         return;
      }
      host_.beginEdit(id_);
      dragging_ = true;
      set(e);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (dragging_)
         set(e);
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (!dragging_)
         return;
      dragging_ = false;
      host_.endEdit(id_);
      if (onDone)
         onDone();
   }
   std::function<void()> onDone;

private:
   void set(const visage::MouseEvent &e) {
      const float s = ui::designScale(*this);
      const float f = std::clamp((e.position.x / s - 7) / (width() / s - 14), 0.0f, 1.0f);
      host_.setParam(id_, kParams[id_].min + f * (kParams[id_].max - kParams[id_].min));
      redraw();
   }
   EditorHost &host_;
   clap_id id_;
   std::string name_, left_, right_;
   unsigned colour_;
   bool dragging_ = false;
};

// ---- GENERATE ----

// Twelve genre tiles, each with its groove in miniature. Click picks a genre (its first style);
// shift-click blends a second in. The genre picked last stands in for the twelfth when it isn't one
// of them, so the tile lit is always the genre playing.
class GenreTiles : public visage::Frame {
public:
   explicit GenreTiles(EditorHost &host) : host_(host) {}
   std::vector<int> genres() const {
      std::vector<int> g(std::begin(kTileGenres), std::end(kTileGenres));
      const int now = genre();
      if (std::find(g.begin(), g.end(), now) == g.end())
         g.back() = now;
      return g;
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const std::vector<int> g = genres();
      const int blend = static_cast<int>(std::lround(host_.paramValue(kBlendGenre))) - 1;
      for (int i = 0; i < 12; ++i) {
         const float x = (i % 6) * kTileW, y = (i / 6) * kTileH;
         const bool on = g[static_cast<size_t>(i)] == genre(), mixed = g[static_cast<size_t>(i)] == blend;
         c.setColor(on ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, (kTileW - 5) * s, (kTileH - 5) * s, 5 * s);
         c.setColor(on ? kOrange : mixed ? kPurple : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, y * s, (kTileW - 5) * s, (kTileH - 5) * s, 5 * s, on || mixed ? 1.5f * s : 1.0f);
         const Genre &gen = genreAt(g[static_cast<size_t>(i)]);
         label(c, s, gen.name, x + 7, y + 5, kTileW - 14, on ? kOrange : kWhite, visage::Font::kLeft, 10);
         if (mixed)
            label(c, s, pct(host_.paramValue(kBlend)) + "%", x + 7, y + 5, kTileW - 19, kPurple, visage::Font::kRight, 8);
         const Pattern p = fromGroove(gen.grooves.front());
         drawThumb(c, s, p, gen.kit, x + 6, y + 23, kTileW - 17, kTileH - 34);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int col = static_cast<int>(e.position.x / s / kTileW), row = static_cast<int>(e.position.y / s / kTileH);
      if (col < 0 || col > 5 || row < 0 || row > 1)
         return;
      const int g = genres()[static_cast<size_t>(row * 6 + col)];
      if (e.isShiftDown()) {
         if (onBlend)
            onBlend(g);
      } else if (onPick)
         onPick(g);
   }
   std::function<void(int)> onPick, onBlend;
   static constexpr float kTileW = 84.5f, kTileH = 70.0f;

private:
   int genre() const { return static_cast<int>(std::lround(host_.paramValue(kGenre))); }
   EditorHost &host_;
};

// The player button and the percussionist chip.
class PlayersRow : public visage::Frame {
public:
   explicit PlayersRow(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const int who = std::clamp(static_cast<int>(std::lround(host_.paramValue(kPlayer))), 0, kDrummers - 1);
      const int perc = static_cast<int>(std::lround(host_.paramValue(kPercussionist)));
      c.setColor(host_.playersOpen ? kSlate : t.button);
      c.roundedRectangle(0, 0, 200 * s, height(), 5 * s);
      c.setColor(host_.playersOpen ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, 200 * s, height(), 5 * s, 1.0f);
      drawSticks(c, s, 15, height() / s / 2);
      label(c, s, kDrummerList[who].name, 30, 6, 120, kWhite, visage::Font::kLeft, 11);
      label(c, s, "drummer", 112, 7.5f, 70, 0, visage::Font::kRight, 8.5f);
      c.setColor(ui::defaultTheme().dimText);
      c.triangle(184 * s, (height() / s / 2 - 2) * s, 192 * s, (height() / s / 2 - 2) * s, 188 * s, (height() / s / 2 + 3) * s);
      const float x = 208;
      c.setColor(alpha(kGreen, perc ? 0.9f : 0.45f));
      dashed(c, x * s, 0, (width() / s - x) * s, height(), 4 * s, 1.0f * s);
      drawHand(c, s, x + 13, height() / s / 2, perc ? kGreen : kFaint);
      if (perc) {
         label(c, s, kPercussionistList[perc - 1].name, x + 26, 6, 110, kWhite, visage::Font::kLeft, 11);
         label(c, s, "percussion \xC2\xB7 ch " + std::to_string(static_cast<int>(std::lround(host_.paramValue(kPercChannel)))),
               x + 26, 7.5f, width() / s - x - 46, 0, visage::Font::kRight, 8);
         label(c, s, "\xC3\x97", width() / s - 16, 5, 12, 0, visage::Font::kLeft, 11);
      } else
         label(c, s, "+ add a percussionist", x + 26, 7, 160, kFaint, visage::Font::kLeft, 10);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s;
      const int perc = static_cast<int>(std::lround(host_.paramValue(kPercussionist)));
      if (x > width() / s - 22 && perc) {
         if (onClearPerc)
            onClearPerc();
      } else if (onOpen)
         onOpen(x >= 208);
   }
   std::function<void(bool percussion)> onOpen;
   std::function<void()> onClearPerc;

   static void drawSticks(visage::Canvas &c, float s, float cx, float cy) {
      c.setColor(kWarm);
      c.circle((cx - 9) * s, (cy - 9) * s, 18 * s);
      c.setColor(kOrange);
      c.ring((cx - 9) * s, (cy - 9) * s, 18 * s, 1.2f * s);
      c.segment((cx - 4.5f) * s, (cy - 4.5f) * s, (cx + 4.5f) * s, (cy + 4.5f) * s, 1.6f * s, true);
      c.segment((cx + 4.5f) * s, (cy - 4.5f) * s, (cx - 4.5f) * s, (cy + 4.5f) * s, 1.6f * s, true);
   }
   static void drawHand(visage::Canvas &c, float s, float cx, float cy, unsigned col) {
      c.setColor(col);
      for (int i = 0; i < 4; ++i)
         c.roundedRectangle((cx - 5 + i * 2.6f) * s, (cy - 6 + (i == 0 || i == 3 ? 1.5f : 0)) * s, 2 * s, 7 * s, 1 * s);
      c.roundedRectangle((cx - 5) * s, (cy - 1) * s, 10.4f * s, 6.5f * s, 2.5f * s);
   }

private:
   EditorHost &host_;
};

// THE SONG: the seven sections side by side, each as wide as its bars, with its groove, its energy
// (drag it), its dice, lock and MUTATE.
class SongStrip : public visage::Frame {
public:
   explicit SongStrip(EditorHost &host) : host_(host) {}

   struct Box {
      float x, w;
   };
   std::vector<Box> boxes() const {
      const float s = ui::designScale(*this);
      const float total = width() / s, gap = 6;
      int bars = 0;
      for (const Section &sec : host_.song().sections)
         bars += std::max<int>(1, sec.bars);
      // Each section gets a minimum, and the rest is shared by bars.
      const float minW = 104;
      const float spare = total - gap * (kSections - 1) - minW * kSections;
      std::vector<Box> b;
      float x = 0;
      for (const Section &sec : host_.song().sections) {
         const float w = minW + spare * std::max<int>(1, sec.bars) / static_cast<float>(bars);
         b.push_back({x, w});
         x += w + gap;
      }
      return b;
   }

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float h = height() / s;
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const int kit = kitOf(host_, false);
      const std::vector<Box> b = boxes();
      for (int i = 0; i < kSections; ++i) {
         const Section &sec = host_.song().sections[static_cast<size_t>(i)];
         const float x = b[static_cast<size_t>(i)].x, w = b[static_cast<size_t>(i)].w;
         const bool sel = host_.mutateSection == i;
         c.setColor(sel ? kWarm : kCell);
         c.roundedRectangle(x * s, 0, w * s, h * s, 5 * s);
         c.setColor(sel ? kOrange : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, 0, w * s, h * s, 5 * s, sel ? 1.5f * s : 1.0f);
         label(c, s, kSectionCaps[i], x + 8, 6, w - 16, sel ? kOrange : kWhite, visage::Font::kLeft, 10.5f);
         label(c, s, std::to_string(sec.bars) + " bars", x + 8, 7, w - 16, 0, visage::Font::kRight, 8.5f);
         const bool playing = f.running && f.section == i;
         drawThumb(c, s, sec.vars[0], kit, x + 6, 25, w - 12, 64, playing ? f.step : -1);
         // Energy: a dot and the number, dragged up or down.
         const double e = host_.paramValue(static_cast<clap_id>(kSectionEnergy + i));
         c.setColor(playing ? kGreen : kOrange);
         c.circle((x + 8) * s, 96 * s, 7 * s);
         label(c, s, "energy " + pct(e), x + 19, 93.5f, 80, dragging_ == i ? kWhite : 0, visage::Font::kLeft, 9);
         if (sec.generation > 0)
            label(c, s, "gen " + std::to_string(sec.generation + 1), x + 8, 94, w - 16, kPurple, visage::Font::kRight, 8);
         // Dice, lock, mutate.
         icon(c, s, x + 6, h - 23, 0, false);
         icon(c, s, x + 34, h - 23, 1, sec.locked != 0);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s, h = height() / s;
      const std::vector<Box> b = boxes();
      for (int i = 0; i < kSections; ++i) {
         const float bx = b[static_cast<size_t>(i)].x, w = b[static_cast<size_t>(i)].w;
         if (x < bx || x >= bx + w)
            continue;
         const float lx = x - bx;
         if (y >= h - 25) {
            if (lx < 30) {
               if (onDice)
                  onDice(i);
               return;
            }
            if (lx < 58) {
               if (onLock)
                  onLock(i);
               return;
            }
         }
         if (y >= 88 && y < 104 && lx < 80) {
            dragging_ = i;
            dragY_ = y;
            dragFrom_ = host_.paramValue(static_cast<clap_id>(kSectionEnergy + i));
            host_.beginEdit(static_cast<clap_id>(kSectionEnergy + i));
            return;
         }
         if (onSelect)
            onSelect(i);
         return;
      }
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (dragging_ < 0)
         return;
      const float s = ui::designScale(*this);
      const double v = std::clamp(dragFrom_ + (dragY_ - e.position.y / s) / 100.0, 0.0, 1.0);
      host_.setParam(static_cast<clap_id>(kSectionEnergy + dragging_), v);
      redraw();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (dragging_ < 0)
         return;
      const int i = dragging_;
      dragging_ = -1;
      host_.endEdit(static_cast<clap_id>(kSectionEnergy + i));
      if (onEnergy)
         onEnergy(i);
   }

   const PlayFrame *frame = nullptr;
   std::function<void(int)> onSelect, onDice, onLock, onEnergy;

private:
   static void icon(visage::Canvas &c, float s, float x, float y, int which, bool lit) {
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(lit ? kWarm : t.button);
      c.roundedRectangle(x * s, y * s, 24 * s, 17 * s, 4 * s);
      c.setColor(lit ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(x * s, y * s, 24 * s, 17 * s, 4 * s, 1.0f);
      const float cx = (x + 12) * s, cy = (y + 8.5f) * s;
      if (which == 0) {
         c.setColor(kWhite);
         ui::drawDie(c, cx, cy, 10 * s);
      } else {
         c.setColor(lit ? kOrange : t.dimText);
         c.roundedRectangle(cx - 3.5f * s, cy - 0.5f * s, 7 * s, 5.5f * s, 1.2f * s);
         c.ring(cx - 2.6f * s, cy - 5.2f * s, 5.2f * s, 1.1f * s);
      }
   }
   EditorHost &host_;
   int dragging_ = -1;
   float dragY_ = 0;
   double dragFrom_ = 0;
};

// MUTATE: the section's groove now, and five children, each tagged with how it differs. Click a
// child to put it in the song; "back" takes the one before.
class MutateRow : public visage::Frame {
public:
   explicit MutateRow(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float h = height() / s;
      const int kit = kitOf(host_, false);
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      // NOW.
      c.setColor(kWarm);
      c.roundedRectangle(0, 0, kNowW * s, h * s, 5 * s);
      c.setColor(kOrange);
      c.roundedRectangleBorder(0, 0, kNowW * s, h * s, 5 * s, 1.5f * s);
      label(c, s, "NOW \xC2\xB7 in the song", 9, 6, 200, kOrange, visage::Font::kLeft, 9.5f);
      drawThumb(c, s, sec.vars[0], kit, 9, 23, kNowW - 18, h - 46);
      if (!host_.parents.empty())
         label(c, s, "< back to gen " + std::to_string(sec.generation), 9, h - 18, 110, 0, visage::Font::kLeft, 9);
      label(c, s, "click a child to put it in the song", 9, h - 18, kNowW - 18, kFaint, visage::Font::kRight, 8);
      c.setColor(kFaint);
      c.triangle((kNowW + 7) * s, (h / 2 - 6) * s, (kNowW + 7) * s, (h / 2 + 6) * s, (kNowW + 15) * s, (h / 2) * s);
      // The children.
      for (int i = 0; i < 5 && i < static_cast<int>(host_.litter.size()); ++i) {
         const float x = kNowW + 22 + i * kChildW;
         const bool hover = hover_ == i;
         c.setColor(kCell);
         c.roundedRectangle(x * s, 0, (kChildW - 6) * s, h * s, 5 * s);
         c.setColor(hover ? kWhite : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, 0, (kChildW - 6) * s, h * s, 5 * s, hover ? 1.5f * s : 1.0f);
         std::string tag = host_.tags[static_cast<size_t>(i)];
         if (tag.size() > 26)
            tag = tag.substr(0, 25) + "\xE2\x80\xA6";
         label(c, s, tag, x + 8, 6, kChildW - 20, kWhite, visage::Font::kLeft, 9.5f);
         drawThumb(c, s, host_.litter[static_cast<size_t>(i)], kit, x + 8, 23, kChildW - 22, h - 46);
         label(c, s, hover ? "click to use" : "", x + 8, h - 18, kChildW - 22, kOrange, visage::Font::kLeft, 8.5f);
      }
   }
   void mouseMove(const visage::MouseEvent &e) override {
      const int i = childAt(e);
      if (i != hover_) {
         hover_ = i;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = -1;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int i = childAt(e);
      if (i >= 0) {
         if (onPick)
            onPick(i);
      } else if (e.position.x / s < 120 && e.position.y / s > height() / s - 22 && onBack)
         onBack();
   }
   std::function<void(int)> onPick;
   std::function<void()> onBack;
   static constexpr float kNowW = 228, kChildW = 140;

private:
   int childAt(const visage::MouseEvent &e) const {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kNowW - 22;
      if (x < 0)
         return -1;
      const int i = static_cast<int>(x / kChildW);
      return i < static_cast<int>(host_.litter.size()) && i < 5 && std::fmod(x, kChildW) < kChildW - 6 ? i : -1;
   }
   EditorHost &host_;
   int hover_ = -1;
};

// ---- EDIT ----

// The pattern being edited: a lane for each pad it plays (and any added), a cell per step. Click a
// cell to add or take away a hit, drag up or down on a hit for its velocity, right-click for its
// ratchet, chance and accent. Under it, the picked lane's velocity, chance and nudge per step.
class EditGrid : public visage::Frame {
public:
   explicit EditGrid(EditorHost &host) : host_(host) {}

   // The pattern being edited, fresh from the song (edits to it go back by commit()).
   Pattern &pattern() {
      load();
      return pick(work_);
   }
   void load() { work_ = host_.song(); }

   static Pattern &pick(Song &song, int section, int var) {
      Section &sec = song.sections[static_cast<size_t>(section)];
      return var < kVariations ? sec.vars[static_cast<size_t>(var)] : var == kVariations ? sec.fill : sec.perc;
   }
   Pattern &pick(Song &song) { return pick(song, host_.editSection, host_.editVar); }
   // An edit to the percussion part: it plays as edited, its own variations and fill set aside.
   static void edited(Song &song, int section, int var) {
      if (var != kVariations + 1)
         return;
      Section &sec = song.sections[static_cast<size_t>(section)];
      for (Pattern &p : sec.percVars)
         p.clear();
      sec.percFill.clear();
   }

   std::vector<int> lanes() {
      const uint16_t added = host_.editVar == 5 ? 0 : host_.added;
      return lanesOf(pattern(), added);
   }
   int kit() const { return kitOf(host_, host_.editVar == 5); }
   // Where ADD A PAD's button ends, down from the grid's top.
   float addPadBottom() { return kTop + rowH(lanes().size()) * lanes().size() + 30; }

   void draw(visage::Canvas &c) override {
      load();
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Pattern &p = pattern();
      const std::vector<int> ls = lanes();
      const int steps = p.steps;
      const float gx = kLabelW, gw = width() / s - kLabelW;
      const float cw = gw / steps;
      const float rh = rowH(ls.size());
      // Step numbers.
      for (int st = 0; st < steps; ++st)
         label(c, s, std::to_string(st + 1), gx + st * cw, 0, cw, st % 4 == 0 ? kWhite : 0, visage::Font::kCenter, 8.5f);
      const int kitNow = kit();
      for (size_t li = 0; li < ls.size(); ++li) {
         const int pad = ls[li];
         const float y = kTop + li * rh;
         const bool sel = host_.editPad == pad;
         if (sel) {
            c.setColor(0xff262b33);
            c.roundedRectangle(0, (y - 1) * s, (kLabelW - 6) * s, (rh - 2) * s, 4 * s);
         }
         c.setColor(roleColour(kitNow, pad));
         c.circle(6 * s, (y + rh / 2 - 4.5f) * s, 7 * s);
         label(c, s, padName(kitNow, pad), 18, y + rh / 2 - 8, 90, sel ? kOrange : kWhite, visage::Font::kLeft, 10.5f);
         label(c, s, noteText(kFirstNote + pad), 96, y + rh / 2 - 7, 30, 0, visage::Font::kLeft, 8.5f);
         // M and S.
         const bool m = host_.muted.load() >> pad & 1, so = host_.soloed.load() >> pad & 1;
         for (int k = 0; k < 2; ++k) {
            const bool on = k == 0 ? m : so;
            const float bx = 128 + k * 22;
            c.setColor(on ? (k == 0 ? kSlate : kOrange) : t.button);
            c.roundedRectangle(bx * s, (y + rh / 2 - 8) * s, 18 * s, 16 * s, 3 * s);
            c.setColor(t.panelBorder);
            c.roundedRectangleBorder(bx * s, (y + rh / 2 - 8) * s, 18 * s, 16 * s, 3 * s, 1.0f);
            label(c, s, k == 0 ? "M" : "S", bx, y + rh / 2 - 7, 18, on ? 0xff1b1408u : 0, visage::Font::kCenter, 8.5f);
         }
         for (int st = 0; st < steps; ++st) {
            const float x = gx + st * cw;
            c.setColor(st / 4 % 2 ? 0xff252a31 : kCell);
            c.roundedRectangle((x + 1.5f) * s, (y + 1) * s, (cw - 3) * s, (rh - 4) * s, 3 * s);
            const Hit &h = p.at(pad, st);
            if (!h.on())
               continue;
            const unsigned col = roleColour(kitNow, pad);
            const float full = rh - 4;
            const float hh = h.vel <= 50 ? full * 0.45f : full * (0.55f + 0.45f * h.vel / 127.0f);
            const float yy = y + 1 + full - hh;
            const int r = h.ratchet;
            for (int k = 0; k < r; ++k) {
               const float sw = (cw - 3) / r;
               if (h.chance < 100) {
                  c.setColor(alpha(col, 0.25f));
                  c.fill((x + 1.5f + k * sw) * s, yy * s, (sw - (r > 1 ? 1.5f : 0)) * s, hh * s);
                  c.setColor(col);
                  dashed(c, (x + 1.5f + k * sw) * s, yy * s, (sw - (r > 1 ? 1.5f : 0)) * s, hh * s, 2.5f * s, 1.0f * s);
               } else {
                  c.setColor(alpha(col, 0.35f + 0.65f * h.vel / 127.0f));
                  c.roundedRectangle((x + 1.5f + k * sw) * s, yy * s, (sw - (r > 1 ? 1.5f : 0)) * s, hh * s, 2.5f * s);
               }
            }
            if (h.vel >= kAccent - 4) {
               c.setColor(0xddffffff);
               c.fill((x + 2.5f) * s, yy * s, (cw - 5) * s, 1.6f * s);
            }
            if (h.chance < 100)
               label(c, s, std::to_string(h.chance) + "%", x, y + rh / 2 - 7, cw - 4, col, visage::Font::kRight, 7.5f);
         }
      }
      // The playhead, when this pattern is the one playing.
      if (frame && frame->running && frame->section == host_.editSection && frame->step >= 0 &&
          (host_.editVar == 5 || (host_.editVar == 4) == frame->fill) &&
          (host_.editVar >= 4 || frame->variation == host_.editVar) && frame->step < steps) {
         const float x = gx + frame->step * cw;
         c.setColor(0x99e6e8eb);
         c.roundedRectangleBorder(x * s, (kTop - 4) * s, cw * s, (rh * ls.size() + 4) * s, 4 * s, 1.0f * s);
      }
      // ADD A PAD, and what's under it.
      const float ay = kTop + rh * ls.size() + 8;
      c.setColor(kOrange);
      dashed(c, 4 * s, ay * s, (kLabelW - 16) * s, 22 * s, 3 * s, 1.0f * s);
      label(c, s, "+  ADD A PAD", 4, ay + 4, kLabelW - 16, kOrange, visage::Font::kCenter, 10);
      label(c, s, std::to_string(p.padsUsed()) + " of 16 pads \xC2\xB7 a lane shows only when the pattern plays its pad", gx,
            ay + 5, 400, 0, visage::Font::kLeft, 8.5f);
      // The picked lane's values.
      const float vy = height() / s - kValuesH;
      c.setColor(kRule);
      c.fill(0, (vy - 8) * s, width(), 1.0f * s);
      if (host_.editPad < 0 || !(p.uses(host_.editPad) || (host_.added >> host_.editPad & 1))) {
         label(c, s, "Click a lane's name to see its velocity, chance and nudge here.", 0, vy + 4, 500, kFaint);
         return;
      }
      const int pad = host_.editPad;
      label(c, s, upper(padName(kitNow, pad)), 0, vy, 200, kOrange, visage::Font::kLeft, 10);
      const char *rows[3] = {"VELOCITY", "CHANCE", "NUDGE"};
      for (int r = 0; r < 3; ++r) {
         const float y = vy + 20 + r * 23;
         label(c, s, rows[r], 0, y + 3, 120, 0, visage::Font::kLeft, 9.5f);
         for (int st = 0; st < steps; ++st) {
            const float x = gx + st * cw;
            const Hit &h = p.at(pad, st);
            c.setColor(kWell);
            c.roundedRectangle((x + 1.5f) * s, y * s, (cw - 3) * s, 19 * s, 3 * s);
            c.setColor(t.panelBorder);
            c.roundedRectangleBorder((x + 1.5f) * s, y * s, (cw - 3) * s, 19 * s, 3 * s, 1.0f);
            if (!h.on()) {
               label(c, s, "-", x, y + 2, cw, kFaint, visage::Font::kCenter, 8);
               continue;
            }
            std::string v;
            unsigned col = kWhite;
            if (r == 0) {
               v = std::to_string(h.vel);
               c.setColor(roleColour(kitNow, pad));
               c.fill((x + 5) * s, (y + 16) * s, (cw - 10) * h.vel / 127.0f * s, 1.5f * s);
            } else if (r == 1) {
               v = std::to_string(h.chance) + "%";
               if (h.chance < 100)
                  col = roleColour(kitNow, pad);
            } else
               v = h.nudge == 0 ? "0" : (h.nudge > 0 ? "+" : "") + std::to_string(h.nudge) + " ms";
            label(c, s, v, x, y + 2.5f, cw, col, visage::Font::kCenter, 8.5f);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      load();
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      const std::vector<int> ls = lanes();
      Pattern &p = pattern();
      const float gx = kLabelW, gw = width() / s - kLabelW, cw = gw / p.steps;
      const float rh = rowH(ls.size());
      drag_ = {};
      // The value rows.
      const float vy = height() / s - kValuesH + 20;
      if (y >= vy && host_.editPad >= 0 && x >= gx) {
         const int row = static_cast<int>((y - vy) / 23), st = static_cast<int>((x - gx) / cw);
         if (row < 3 && st >= 0 && st < p.steps && p.at(host_.editPad, st).on()) {
            Hit &h = p.at(host_.editPad, st);
            if (e.repeatClickCount() >= 2) {
               if (row == 0)
                  h.vel = kNormal;
               else if (row == 1)
                  h.chance = 100;
               else
                  h.nudge = 0;
               commit();
               return;
            }
            drag_ = {true, host_.editPad, st, row + 1, y, row == 0 ? h.vel : row == 1 ? h.chance : h.nudge};
         }
         return;
      }
      // ADD A PAD.
      const float ay = kTop + rh * ls.size() + 8;
      if (y >= ay && y < ay + 22 && x < kLabelW) {
         if (onAddPad)
            onAddPad();
         return;
      }
      const int li = static_cast<int>((y - kTop) / rh);
      if (y < kTop || li < 0 || li >= static_cast<int>(ls.size()))
         return;
      const int pad = ls[static_cast<size_t>(li)];
      if (x < kLabelW) {
         if (x >= 128 && x < 146)
            host_.muted.store(static_cast<uint16_t>(host_.muted.load() ^ (1u << pad)));
         else if (x >= 150 && x < 168)
            host_.soloed.store(static_cast<uint16_t>(host_.soloed.load() ^ (1u << pad)));
         else
            host_.editPad = pad;
         redraw();
         return;
      }
      const int st = static_cast<int>((x - gx) / cw);
      if (st < 0 || st >= p.steps)
         return;
      host_.editPad = pad;
      Hit &h = p.at(pad, st);
      if (e.isRightButton()) {
         if (h.on() && onHitMenu)
            onHitMenu(pad, st, e.position);
         return;
      }
      if (h.on()) {
         // A drag sets its velocity; a click without one takes it away.
         drag_ = {true, pad, st, 0, y, h.vel};
         removeOnUp_ = true;
         return;
      }
      h = Hit{};
      h.vel = kNormal;
      removeOnUp_ = false;
      drag_ = {true, pad, st, 0, y, h.vel};
      commit();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (!drag_.on)
         return;
      const float s = ui::designScale(*this);
      const float dy = drag_.y - e.position.y / s;
      if (std::abs(dy) > 2)
         removeOnUp_ = false;
      load();
      Hit &h = pattern().at(drag_.pad, drag_.step);
      if (!h.on())
         return;
      switch (drag_.what) {
      case 0: case 1: h.vel = static_cast<uint8_t>(std::clamp(drag_.from + static_cast<int>(dy), 1, 127)); break;
      case 2: h.chance = static_cast<uint8_t>(std::clamp(drag_.from + static_cast<int>(dy), 0, 100)); break;
      default: h.nudge = static_cast<int8_t>(std::clamp(drag_.from + static_cast<int>(dy / 3), -kMaxNudge, kMaxNudge)); break;
      }
      commit();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (drag_.on && removeOnUp_) {
         load();
         pattern().at(drag_.pad, drag_.step) = Hit{};
         commit();
      }
      drag_ = {};
      removeOnUp_ = false;
   }

   void commit() {
      edited(work_, host_.editSection, host_.editVar);
      host_.setSong(work_);
      redraw();
      if (onChange)
         onChange();
   }

   const PlayFrame *frame = nullptr;
   std::function<void()> onAddPad, onChange;
   std::function<void(int pad, int step, visage::Point at)> onHitMenu;
   static constexpr float kLabelW = 176, kTop = 16, kValuesH = 92, kGridH = 476;

private:
   float rowH(size_t lanes) const {
      // From the grid's design height, so it is right before the panel is laid out.
      const float room = kGridH - kTop - kValuesH - 46;
      return std::clamp(room / std::max<size_t>(1, lanes), 14.0f, 40.0f);
   }
   struct Drag {
      bool on = false;
      int pad = 0, step = 0, what = 0; // what: 0 the cell, 1 velocity, 2 chance, 3 nudge
      float y = 0;
      int from = 0;
   };
   EditorHost &host_;
   Song work_;
   Drag drag_;
   bool removeOnUp_ = false;
};

// ---- PLAY ----

// SECTIONS: a tile each, with its note, groove, energy, and its variations and fill. Click a tile
// to switch to it (at SWITCH AT), a number for that variation, FILL for a fill.
class SectionTiles : public visage::Frame {
public:
   explicit SectionTiles(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float h = height() / s, w = tileW();
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const int kit = kitOf(host_, false);
      Settings st;
      st.noteMap = static_cast<int>(std::lround(host_.paramValue(kNoteMap)));
      st.noteOctave = static_cast<int>(std::lround(host_.paramValue(kNoteOctave)));
      for (int a = 0; a < kActionCount; ++a)
         st.learn[static_cast<size_t>(a)] = static_cast<int>(std::lround(host_.paramValue(static_cast<clap_id>(kLearnNote + a))));
      for (int i = 0; i < kSections; ++i) {
         const float x = i * (w + 6);
         const Section &sec = host_.song().sections[static_cast<size_t>(i)];
         const bool playing = f.section == i, queued = f.queued == i;
         c.setColor(playing ? kWarm : kCell);
         c.roundedRectangle(x * s, 6 * s, w * s, (h - 6) * s, 6 * s);
         if (queued) {
            c.setColor(kOrange);
            dashed(c, x * s, 6 * s, w * s, (h - 6) * s, 3 * s, 1.0f * s);
            c.roundedRectangle((x + w / 2 - 15) * s, 0, 30 * s, 12 * s, 2 * s);
            label(c, s, "NEXT", x + w / 2 - 15, 0.5f, 30, 0xff1b1408u, visage::Font::kCenter, 7);
         } else {
            c.setColor(playing ? kOrange : ui::defaultTheme().panelBorder);
            c.roundedRectangleBorder(x * s, 6 * s, w * s, (h - 6) * s, 6 * s, playing ? 1.5f * s : 1.0f);
         }
         label(c, s, kSectionCaps[i], x + 11, 15, w - 20, playing ? kOrange : kWhite, visage::Font::kLeft, 12);
         label(c, s, noteText(st.noteFor(i)), x + 11, 17, w - 20, 0, visage::Font::kRight, 8.5f);
         const int show = playing ? (f.fill ? -1 : f.variation) : 0;
         const Pattern &p = show < 0 ? sec.fill : sec.vars[static_cast<size_t>(std::max(0, show))];
         drawThumb(c, s, p, kit, x + 9, 38, w - 18, 52, playing && f.running ? f.step : -1);
         c.setColor(ui::defaultTheme().track);
         c.roundedRectangle((x + 9) * s, 97 * s, (w - 18) * s, 3 * s, 1.5f * s);
         c.setColor(kOrange);
         c.roundedRectangle((x + 9) * s, 97 * s, std::max(3.0f, (w - 18) * sec.energy / 100.0f) * s, 3 * s, 1.5f * s);
         // Variations and fill.
         const float bw = (w - 18 - 4 * 3) / 5.4f;
         for (int k = 0; k < 5; ++k) {
            const float bx = x + 9 + k * (bw + 3), by = h - 26, ww = k == 4 ? bw * 1.4f : bw;
            const bool lit = playing && (k == 4 ? f.fill : !f.fill && f.variation == k);
            const bool qd = (playing || queued) && k < 4 && f.queuedVar == k;
            c.setColor(lit ? kSlate : ui::defaultTheme().button);
            c.roundedRectangle(bx * s, by * s, ww * s, 17 * s, 3 * s);
            c.setColor(qd ? kOrange : ui::defaultTheme().panelBorder);
            c.roundedRectangleBorder(bx * s, by * s, ww * s, 17 * s, 3 * s, 1.0f);
            label(c, s, k == 4 ? "FILL" : std::to_string(k + 1), bx, by + 2, ww, lit ? kWhite : 0, visage::Font::kCenter, 8.5f);
         }
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float w = tileW(), x = e.position.x / s, y = e.position.y / s, h = height() / s;
      const int i = static_cast<int>(x / (w + 6));
      if (i < 0 || i >= kSections || std::fmod(x, w + 6) > w || !onAction)
         return;
      if (y >= h - 28) {
         const float bw = (w - 18 - 4 * 3) / 5.4f;
         const int k = std::clamp(static_cast<int>((std::fmod(x, w + 6) - 9) / (bw + 3)), 0, 4);
         if (frame && frame->section != i)
            onAction(i);
         onAction(k == 4 ? kActFill : kActVar1 + k);
         return;
      }
      onAction(i);
   }
   const PlayFrame *frame = nullptr;
   std::function<void(int)> onAction;

private:
   float tileW() const { return (width() / ui::designScale(*this) - 6 * (kSections - 1)) / kSections; }
   EditorHost &host_;
};

// NOW PLAYING: the pattern playing, lane by lane, with the playhead.
class NowPlaying : public visage::Frame {
public:
   explicit NowPlaying(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const Section &sec = host_.song().sections[static_cast<size_t>(std::clamp(f.section, 0, kSections - 1))];
      const Pattern &p = f.fill ? sec.fill : sec.vars[static_cast<size_t>(std::clamp(f.variation, 0, 3))];
      const int kit = kitOf(host_, false);
      const std::vector<int> ls = lanesOf(p);
      const float lw = 102, gw = width() / s - lw, cw = gw / std::max<int>(1, p.steps);
      const float rh = std::min(19.0f, height() / s / std::max<size_t>(1, ls.size()));
      for (size_t li = 0; li < ls.size(); ++li) {
         const int pad = ls[li];
         const float y = li * rh;
         c.setColor(roleColour(kit, pad));
         c.circle(0, (y + rh / 2 - 3) * s, 6 * s);
         label(c, s, padName(kit, pad), 11, y + rh / 2 - 7.5f, lw - 12, kWhite, visage::Font::kLeft, 10);
         for (int st = 0; st < p.steps; ++st) {
            const float x = lw + st * cw;
            const Hit &h = p.at(pad, st);
            c.setColor(kCell);
            c.roundedRectangle((x + 1) * s, (y + 1) * s, (cw - 2) * s, (rh - 3) * s, 2 * s);
            if (!h.on())
               continue;
            const float full = rh - 3, hh = h.vel <= 50 ? full * 0.45f : full * (0.5f + 0.5f * h.vel / 127.0f);
            c.setColor(alpha(roleColour(kit, pad), h.chance < 100 ? 0.5f : 0.35f + 0.65f * h.vel / 127.0f));
            c.roundedRectangle((x + 1) * s, (y + 1 + full - hh) * s, (cw - 2) * s, hh * s, 2 * s);
         }
      }
      if (f.running && f.step >= 0 && f.step < p.steps) {
         const float x = lw + f.step * cw;
         c.setColor(0x99e6e8eb);
         c.roundedRectangleBorder(x * s, 0, cw * s, (rh * ls.size()) * s, 3 * s, 1.0f * s);
         c.setColor(kWhite);
         c.fill((x + cw / 2) * s, 0, 1.2f * s, (rh * ls.size()) * s);
      }
   }
   const PlayFrame *frame = nullptr;

private:
   EditorHost &host_;
};

// TRANSPORT's big button and readout.
class TransportView : public visage::Frame {
public:
   explicit TransportView(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const ui::Theme &t = ui::defaultTheme();
      const bool playing = f.running;
      c.setColor(playing ? kWarm : t.button);
      c.roundedRectangle(0, 0, 70 * s, 60 * s, 7 * s);
      c.setColor(playing ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, 70 * s, 60 * s, 7 * s, 1.0f);
      c.setColor(playing ? kOrange : t.dimText);
      if (playing && !f.following)
         c.roundedRectangle(27 * s, 22 * s, 16 * s, 16 * s, 2 * s);
      else
         c.triangle(28 * s, 20 * s, 28 * s, 40 * s, 44 * s, 30 * s);
      label(c, s, playing && !f.following ? "STOP" : "PLAY", 0, 66, 70, 0, visage::Font::kCenter, 8.5f);
      const unsigned dot = f.following ? kGreen : playing ? kOrange : kFaint;
      c.setColor(dot);
      c.circle(86 * s, 4 * s, 7 * s);
      label(c, s, f.following ? "FOLLOWING HOST" : playing ? "RUNNING FREE" : "STOPPED", 97, 1, 160, dot,
            visage::Font::kLeft, 9.5f);
      char bpm[32];
      std::snprintf(bpm, sizeof(bpm), "%.1f", f.tempo);
      label(c, s, bpm, 84, 17, 90, kWhite, visage::Font::kLeft, 21);
      label(c, s, "BPM", 154, 27, 40, 0, visage::Font::kLeft, 10);
      label(c, s, "bar " + std::to_string(f.bar + 1) + " \xC2\xB7 beat " + std::to_string(f.beat + 1), 84, 50, 160, 0,
            visage::Font::kLeft, 9.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      if (e.position.x / s < 70 && e.position.y / s < 60 && onPlay)
         onPlay();
   }
   const PlayFrame *frame = nullptr;
   std::function<void()> onPlay;

private:
   EditorHost &host_;
};

// OUTPUT's pad map: JKit's 16 pads as JKit lays them out (pad 1 bottom left), each lit as it plays.
class PadMap : public visage::Frame {
public:
   explicit PadMap(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const int kit = kitOf(host_, false);
      const float cw = width() / s / 4, ch = height() / s / 4;
      const Section &sec = host_.song().sections[static_cast<size_t>(frame ? std::clamp(frame->section, 0, 6) : 0)];
      uint16_t used = 0;
      for (const Pattern &p : sec.vars)
         for (int pad = 0; pad < kPads; ++pad)
            used |= static_cast<uint16_t>(p.uses(pad) << pad);
      for (int pad = 0; pad < kPads; ++pad) {
         const float x = (pad % 4) * cw, y = (3 - pad / 4) * ch;
         const float glow = lit_[static_cast<size_t>(pad)];
         const unsigned col = roleColour(kit, pad);
         const bool on = used >> pad & 1;
         c.setColor(glow > 0 ? alpha(col, 0.18f + 0.3f * glow) : ui::defaultTheme().button);
         c.roundedRectangle((x + 2) * s, (y + 2) * s, (cw - 4) * s, (ch - 4) * s, 4 * s);
         c.setColor(glow > 0 ? col : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder((x + 2) * s, (y + 2) * s, (cw - 4) * s, (ch - 4) * s, 4 * s, glow > 0 ? 1.4f * s : 1.0f);
         c.setColor(on ? col : alpha(col, 0.35f));
         c.circle((x + 8) * s, (y + ch / 2 - 3) * s, 6 * s);
         label(c, s, padName(kit, pad), x + 18, y + ch / 2 - 7, cw - 20, on ? kWhite : kFaint, visage::Font::kLeft, 9);
      }
   }
   // Lights the pads hit since the last call, and fades the rest.
   void update(const PlayFrame &f) {
      frame = &f;
      bool changed = false;
      for (int pad = 0; pad < kPads; ++pad) {
         float &l = lit_[static_cast<size_t>(pad)];
         if (f.hits[static_cast<size_t>(pad)] != seen_[static_cast<size_t>(pad)]) {
            seen_[static_cast<size_t>(pad)] = f.hits[static_cast<size_t>(pad)];
            l = 1.0f;
            changed = true;
         } else if (l > 0) {
            l = std::max(0.0f, l - 0.12f);
            changed = true;
         }
      }
      if (changed)
         redraw();
   }
   const PlayFrame *frame = nullptr;

private:
   EditorHost &host_;
   std::array<float, kPads> lit_{};
   std::array<uint32_t, kPads> seen_{};
};

// The header's small transport: play, the tempo, and whether it follows the host.
class HeaderTransport : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const bool playing = frame && frame->running;
      c.setColor(playing ? kWarm : t.button);
      c.roundedRectangle(0, 0, 28 * s, 28 * s, 5 * s);
      c.setColor(playing ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, 28 * s, 28 * s, 5 * s, 1.0f);
      c.setColor(playing ? kOrange : t.dimText);
      if (playing && frame && !frame->following)
         c.fill(9 * s, 9 * s, 10 * s, 10 * s);
      else
         c.triangle(10 * s, 8 * s, 10 * s, 20 * s, 20 * s, 14 * s);
      char bpm[32];
      std::snprintf(bpm, sizeof(bpm), "%.0f BPM", frame ? frame->tempo : 120.0);
      label(c, s, bpm, 34, 0, 70, kWhite, visage::Font::kLeft, 10.5f);
      const bool host = frame && frame->following;
      c.setColor(host ? kGreen : playing ? kOrange : kFaint);
      c.circle(35 * s, 18 * s, 5 * s);
      label(c, s, host ? "HOST" : "FREE", 43, 14.5f, 50, host ? kGreen : playing ? kOrange : 0, visage::Font::kLeft, 8.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (e.position.x < 28 * ui::designScale(*this) && onPlay)
         onPlay();
   }
   const PlayFrame *frame = nullptr;
   std::function<void()> onPlay;
};

// ---- overlays ----

// PLAYERS: every drummer and percussionist, with what each plays like.
class PlayersOverlay : public visage::Frame {
public:
   explicit PlayersOverlay(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(0x88000000);
      c.fill(0, 0, width(), height());
      const float x0 = kX, y0 = kY;
      c.setColor(ui::defaultTheme().panel);
      c.roundedRectangle(x0 * s, y0 * s, kWBox * s, kHBox * s, 8 * s);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(x0 * s, y0 * s, kWBox * s, kHBox * s, 8 * s, 1.0f);
      label(c, s, "PLAYERS", x0 + 16, y0 + 12, 100, kOrange, visage::Font::kLeft, 11.5f);
      label(c, s, "A player is how the pattern is played: its timing, ghost notes, fills and dynamics. Click a drummer to "
                  "swap hands; click a percussionist to add or drop a second part.",
            x0 + 110, y0 + 14, kWBox - 150, 0, visage::Font::kLeft, 8.5f);
      label(c, s, "\xC3\x97", x0 + kWBox - 26, y0 + 10, 16, 0, visage::Font::kLeft, 13);
      const int drummer = static_cast<int>(std::lround(host_.paramValue(kPlayer)));
      const int perc = static_cast<int>(std::lround(host_.paramValue(kPercussionist)));
      for (int i = 0; i < 12; ++i) {
         const bool isPerc = i >= 6;
         const Player &p = isPerc ? kPercussionistList[i - 6] : kDrummerList[i];
         const float x = x0 + 14 + (i % 3) * kCardW, y = y0 + 40 + (i / 3) * kCardH;
         const bool on = isPerc ? perc == i - 5 : drummer == i;
         const unsigned col = isPerc ? kGreen : kOrange;
         c.setColor(on && !isPerc ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, (kCardW - 8) * s, (kCardH - 8) * s, 6 * s);
         if (on && isPerc) {
            c.setColor(kGreen);
            dashed(c, x * s, y * s, (kCardW - 8) * s, (kCardH - 8) * s, 3 * s, 1.0f * s);
         } else {
            c.setColor(on ? kOrange : ui::defaultTheme().panelBorder);
            c.roundedRectangleBorder(x * s, y * s, (kCardW - 8) * s, (kCardH - 8) * s, 6 * s, on ? 1.5f * s : 1.0f);
         }
         if (isPerc) {
            c.setColor(0xff1f2a25);
            c.circle((x + 10) * s, (y + 10) * s, 24 * s);
            PlayersRow::drawHand(c, s, x + 22, y + 22, kGreen);
         } else
            PlayersRow::drawSticks(c, s, x + 22, y + 22);
         label(c, s, p.name, x + 42, y + 8, 160, on && !isPerc ? kOrange : kWhite, visage::Font::kLeft, 11.5f);
         label(c, s, isPerc ? "PERCUSSIONIST" : "DRUMMER", x + 42, y + 9, kCardW - 62, col, visage::Font::kRight, 7.5f);
         label(c, s, p.blurb, x + 42, y + 26, kCardW - 60, kTitle, visage::Font::kLeft, 9);
         label(c, s, p.genres, x + 42, y + 40, kCardW - 60, 0, visage::Font::kLeft, 8.5f);
         const char *traits[4] = {"LOOSE", "BUSY", "GHOSTS", "FILLS"};
         const double v[4] = {p.loose, p.busy, p.ghosts, p.fills};
         const float tw = (kCardW - 32) / 4;
         for (int k = 0; k < 4; ++k) {
            const float tx = x + 10 + k * tw;
            label(c, s, traits[k], tx, y + 58, tw, 0, visage::Font::kLeft, 7.5f);
            c.setColor(ui::defaultTheme().track);
            c.roundedRectangle(tx * s, (y + 74) * s, (tw - 10) * s, 2.5f * s, 1 * s);
            c.setColor(col);
            c.roundedRectangle(tx * s, (y + 74) * s, std::max(2.0f, static_cast<float>((tw - 10) * v[k])) * s, 2.5f * s, 1 * s);
         }
         if (on)
            label(c, s, isPerc ? "+ added, out on ch " + std::to_string(static_cast<int>(std::lround(host_.paramValue(kPercChannel))))
                               : "on now",
                  x + 10, y + kCardH - 24, kCardW - 28, col, visage::Font::kRight, 8);
      }
      label(c, s, "Tip: a drummer and a percussionist can play together. The percussionist goes out on its own channel to a "
                  "second JKit set to PERCUSSION.",
            x0 + 16, y0 + kHBox - 20, kWBox - 32, 0, visage::Font::kLeft, 8.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kX - 14, y = e.position.y / s - kY - 40;
      if (x < -14 || y < -40 || x > kWBox - 14 || y > kHBox - 40 || (y < 0 && x > kWBox - 50)) {
         if (onClose)
            onClose();
         return;
      }
      if (x < 0 || y < 0)
         return;
      const int col = static_cast<int>(x / kCardW), row = static_cast<int>(y / kCardH);
      if (col > 2 || row > 3)
         return;
      const int i = row * 3 + col;
      if (onPick)
         onPick(i);
   }
   std::function<void(int)> onPick; // 0..5 a drummer, 6..11 a percussionist
   std::function<void()> onClose;
   static constexpr float kX = 20, kY = 118, kWBox = 960, kHBox = 494, kCardW = 310, kCardH = 106;

private:
   EditorHost &host_;
};

// The style browser: every genre by family, and the styles of the genre picked.
class BrowserOverlay : public visage::Frame {
public:
   explicit BrowserOverlay(EditorHost &host) : host_(host) {}
   void open() {
      shown_ = static_cast<int>(std::lround(host_.paramValue(kGenre)));
      setVisible(true);
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(0x88000000);
      c.fill(0, 0, width(), height());
      c.setColor(ui::defaultTheme().panel);
      c.roundedRectangle(kX * s, kY * s, kWBox * s, kHBox * s, 8 * s);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(kX * s, kY * s, kWBox * s, kHBox * s, 8 * s, 1.0f);
      label(c, s, "ALL GENRES", kX + 16, kY + 12, 200, kOrange, visage::Font::kLeft, 11.5f);
      label(c, s, std::to_string(genreCount()) + " genres \xC2\xB7 " + std::to_string(styleCount()) +
                     " styles \xC2\xB7 every style is also a preset",
            kX + 120, kY + 14, 400, 0, visage::Font::kLeft, 9);
      label(c, s, "\xC3\x97", kX + kWBox - 26, kY + 10, 16, 0, visage::Font::kLeft, 13);
      const int current = static_cast<int>(std::lround(host_.paramValue(kGenre)));
      for (const Item &it : items()) {
         if (it.genre < 0) {
            label(c, s, upper(kFamilies[it.family]), it.x, it.y, 150, kFaint, visage::Font::kLeft, 8);
            continue;
         }
         const bool on = it.genre == shown_;
         if (on) {
            c.setColor(kSlate);
            c.roundedRectangle((it.x - 5) * s, (it.y - 2) * s, 138 * s, 17 * s, 3 * s);
         }
         label(c, s, genreAt(it.genre).name, it.x, it.y, 110, it.genre == current ? kOrange : on ? kWhite : kTitle,
               visage::Font::kLeft, 10);
         label(c, s, std::to_string(genreAt(it.genre).styles.size()), it.x, it.y + 1, 128, 0, visage::Font::kRight, 8.5f);
      }
      // The styles.
      const float lx = kX + kListX;
      c.setColor(ui::defaultTheme().panelBorder);
      c.fill((lx - 14) * s, (kY + 40) * s, 1.0f * s, (kHBox - 52) * s);
      const Genre &g = genreAt(shown_);
      label(c, s, upper(g.name), lx, kY + 42, 200, kOrange, visage::Font::kLeft, 10.5f);
      label(c, s, std::to_string(g.styles.size()) + " styles \xC2\xB7 " + kKitNames[g.kit], lx + 120, kY + 43, 200, 0,
            visage::Font::kLeft, 9);
      const int style = static_cast<int>(std::lround(host_.paramValue(kStyle)));
      for (size_t i = 0; i < g.styles.size(); ++i) {
         const float y = kY + 64 + i * kRowH;
         const bool on = shown_ == current && static_cast<int>(i) == style;
         if (on) {
            c.setColor(kSlate);
            c.roundedRectangle((lx - 6) * s, (y - 2) * s, (kWBox - kListX - 12) * s, (kRowH - 3) * s, 4 * s);
         }
         label(c, s, g.styles[i].name, lx, y, 160, on ? kWhite : kTitle, visage::Font::kLeft, 10.5f);
         label(c, s, std::string(g.styles[i].tag) + " \xC2\xB7 " + std::to_string(g.styles[i].bpm), lx, y + 14, 160, 0,
               visage::Font::kLeft, 8.5f);
         Rng rng(i + 1);
         Pattern p = fromGroove(g.grooves[static_cast<size_t>(g.styles[i].groove)]);
         applyMods(p, g.styles[i].mods, g.kit, rng);
         drawThumb(c, s, p, g.kit, lx + 170, y - 1, kWBox - kListX - 196, kRowH - 6);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      if (x < kX || x > kX + kWBox || y < kY || y > kY + kHBox || (y < kY + 32 && x > kX + kWBox - 40)) {
         if (onClose)
            onClose();
         return;
      }
      for (const Item &it : items())
         if (it.genre >= 0 && x >= it.x - 5 && x < it.x + 133 && y >= it.y - 2 && y < it.y + 15) {
            shown_ = it.genre;
            redraw();
            return;
         }
      const float lx = kX + kListX;
      if (x >= lx - 6) {
         const int i = static_cast<int>((y - kY - 62) / kRowH);
         if (i >= 0 && i < static_cast<int>(genreAt(shown_).styles.size()) && onPick)
            onPick(shown_, i);
      }
   }
   std::function<void(int genre, int style)> onPick;
   std::function<void()> onClose;
   static constexpr float kX = 20, kY = 84, kWBox = 960, kHBox = 528, kListX = 600, kRowH = 33;

private:
   struct Item {
      float x, y;
      int family, genre; // genre -1: the family's heading
   };
   std::vector<Item> items() const {
      std::vector<Item> out;
      for (int col = 0; col < 4; ++col) {
         float y = kY + 44;
         for (int fam = col * 2; fam < col * 2 + 2; ++fam) {
            const float x = kX + 18 + col * 146;
            out.push_back({x, y, fam, -1});
            y += 15;
            for (int g = 0; g < genreCount(); ++g)
               if (genreAt(g).family == kFamilies[fam]) {
                  out.push_back({x, y, fam, g});
                  y += 18;
               }
            y += 10;
         }
      }
      return out;
   }
   EditorHost &host_;
   int shown_ = 0;
};

// SECTION NOTES: the keyboard map, or each action's own learned note.
class NotesOverlay : public visage::Frame {
public:
   explicit NotesOverlay(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(t.panel);
      c.roundedRectangle(0, 0, width(), height(), 8 * s);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 8 * s, 1.0f);
      label(c, s, "SECTION NOTES", 16, 12, 200, kOrange, visage::Font::kLeft, 11.5f);
      const bool custom = std::lround(host_.paramValue(kNoteMap)) == 1;
      if (!custom) {
         label(c, s, "One note per section, on any channel or one you pick. Or automate the Section parameter.", 16, 32,
               520, 0, visage::Font::kLeft, 9);
         drawKeys(c, s);
      } else {
         label(c, s, "Press LEARN, then play any note to give it that job. Orange: learned; the rest are as the keyboard map.",
               16, 32, 560, 0, visage::Font::kLeft, 9);
         drawCustom(c, s);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      if (std::lround(host_.paramValue(kNoteMap)) != 1)
         return;
      for (int a = 0; a < kActionCount; ++a) {
         const float rx = rowX(a), ry = rowY(a);
         if (y >= ry && y < ry + kRowH - 3 && x >= rx + 138 && x < rx + 194) {
            if (onLearn)
               onLearn(host_.learnAction == a ? -1 : a);
            return;
         }
      }
   }
   std::function<void(int)> onLearn;
   static constexpr float kKeysY = 52, kRowH = 21;

private:
   float rowX(int a) const { return 16 + (a % 3) * 204.0f; }
   float rowY(int a) const { return kKeysY + 4 + (a / 3) * kRowH; }

   void drawCustom(visage::Canvas &c, float s) {
      const ui::Theme &t = ui::defaultTheme();
      for (int a = 0; a < kActionCount; ++a) {
         const float x = rowX(a), y = rowY(a);
         const bool learning = host_.learnAction == a;
         c.setColor(learning ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, 198 * s, (kRowH - 3) * s, 4 * s);
         c.setColor(learning ? kOrange : t.panelBorder);
         c.roundedRectangleBorder(x * s, y * s, 198 * s, (kRowH - 3) * s, 4 * s, learning ? 1.4f * s : 1.0f);
         label(c, s, upper(kActionNames[a]), x + 8, y + 2.5f, 70, learning ? kOrange : kWhite, visage::Font::kLeft, 9.5f);
         const int note = static_cast<int>(std::lround(host_.paramValue(static_cast<clap_id>(kLearnNote + a))));
         const bool learned = note != 12 + kActionKey[a];
         c.setColor(learned ? kWarm : kWell);
         c.roundedRectangle((x + 76) * s, (y + 2) * s, 58 * s, (kRowH - 7) * s, 3 * s);
         if (learned) {
            c.setColor(kOrange);
            c.roundedRectangleBorder((x + 76) * s, (y + 2) * s, 58 * s, (kRowH - 7) * s, 3 * s, 1.0f);
         }
         label(c, s, learning ? "play\xE2\x80\xA6" : noteText(note), x + 76, y + 3, 58,
               learning ? kOrange : kWhite, visage::Font::kCenter, 9);
         c.setColor(learning ? kOrange : t.button);
         c.roundedRectangle((x + 138) * s, (y + 2) * s, 54 * s, (kRowH - 7) * s, 3 * s);
         label(c, s, learning ? "CANCEL" : "LEARN", x + 138, y + 3, 54, learning ? 0xff1b1408u : kOrange,
               visage::Font::kCenter, 8.5f);
      }
   }

   void drawKeys(visage::Canvas &c, float s) {
      // White keys: the sections then NEXT, PREV, RESTART, STOP; black keys between.
      const int base = 12 * (static_cast<int>(std::lround(host_.paramValue(kNoteOctave))) + 1);
      const int whites[11] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17};
      const float kw = (width() / s - 32) / 12, kh = 150, x0 = 16, y0 = kKeysY;
      const PlayFrame *f = frame;
      auto actionAt = [](int semis) {
         for (int a = 0; a < kActionCount; ++a)
            if (kActionKey[a] == semis)
               return a;
         return -1;
      };
      for (int i = 0; i < 12; ++i) {
         const int semis = i < 11 ? whites[i] : 19;
         const int a = actionAt(semis);
         const bool playing = f && a >= 0 && a <= kActOutro && f->section == a;
         const bool queued = f && a >= 0 && f->queued == a;
         c.setColor(playing ? 0xfff9e3cdu : a >= 0 ? 0xffdfe2e6u : 0xff8a9099u);
         c.roundedRectangle((x0 + i * kw) * s, y0 * s, (kw - 2) * s, kh * s, 3 * s);
         if (queued) {
            c.setColor(kOrange);
            dashed(c, (x0 + i * kw) * s, y0 * s, (kw - 2) * s, kh * s, 3 * s, 1.5f * s);
         }
         if (a >= 0) {
            label(c, s, upper(kActionNames[a]), x0 + i * kw, y0 + kh - 36, kw - 2, a <= kActOutro ? 0xff1b1d22u : 0xff5d636cu,
                  visage::Font::kCenter, 8.5f);
            label(c, s, noteText(base + semis), x0 + i * kw, y0 + kh - 20, kw - 2, 0xff5d636cu, visage::Font::kCenter, 8);
         }
      }
      // Black keys.
      for (int i = 0; i < 11; ++i) {
         const int semis = whites[i] + 1;
         if (i == 2 || i == 6 || i == 9 || semis == 19)
            continue; // no black key after E, B, E
         const int a = actionAt(semis);
         if (semis > 18)
            continue;
         const float x = x0 + (i + 1) * kw - kw * 0.3f;
         const bool lit = f && ((a >= kActVar1 && a <= kActVar4 && f->variation == a - kActVar1 && !f->fill) ||
                                (a == kActFill && f->fill) || (a == kActMuteHats && f->muteHats) ||
                                (a == kActMuteKick && f->muteKick) || (a == kActHold && f->hold));
         c.setColor(lit ? 0xff5a3a1au : 0xff1c1f24u);
         c.roundedRectangle(x * s, y0 * s, kw * 0.6f * s, kh * 0.6f * s, 2.5f * s);
         if (a >= 0) {
            // Two words go on two lines: MUTE over HATS.
            const std::string name = upper(kActionNames[a]);
            const size_t space = name.find(' ');
            const bool two = space != std::string::npos && name.size() > 6;
            const unsigned col = lit ? kOrange : kTitle;
            if (two) {
               label(c, s, name.substr(0, space), x - 4, y0 + kh * 0.6f - 31, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
               label(c, s, name.substr(space + 1), x - 4, y0 + kh * 0.6f - 21, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
            } else
               label(c, s, name, x - 4, y0 + kh * 0.6f - 21, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
         }
      }
   }

public:
   const PlayFrame *frame = nullptr;

private:
   EditorHost &host_;
};

// ADD A PAD: the pads the pattern doesn't play yet.
class AddPadMenu : public visage::Frame {
public:
   explicit AddPadMenu(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(0x55000000);
      c.fill(0, 0, width(), height());
      const std::vector<int> pads = free();
      const float h = 34 + ((pads.size() + 1) / 2) * kRowH;
      c.setColor(ui::defaultTheme().panel);
      c.roundedRectangle(kX * s, top * s, kWBox * s, h * s, 7 * s);
      c.setColor(kOrange);
      c.roundedRectangleBorder(kX * s, top * s, kWBox * s, h * s, 7 * s, 1.0f);
      label(c, s, "ADD A PAD", kX + 12, top + 9, 200, kOrange, visage::Font::kLeft, 10.5f);
      label(c, s, kKitNames[kit()], kX + 12, top + 10, kWBox - 24, 0, visage::Font::kRight, 8.5f);
      for (size_t i = 0; i < pads.size(); ++i) {
         const float x = kX + 12 + (i % 2) * (kWBox / 2 - 6), y = top + 32 + (i / 2) * kRowH;
         const int pad = pads[i];
         c.setColor(roleColour(kit(), pad));
         c.circle(x * s, (y + 4) * s, 7 * s);
         label(c, s, padName(kit(), pad), x + 12, y, 100, kWhite, visage::Font::kLeft, 10);
         label(c, s, noteText(kFirstNote + pad), x + 12, y + 1, kWBox / 2 - 36, 0, visage::Font::kRight, 8.5f);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kX - 6, y = e.position.y / s - top - 30;
      const std::vector<int> pads = free();
      if (x >= 0 && x < kWBox && y >= 0) {
         const size_t i = static_cast<size_t>(y / kRowH) * 2 + (x >= kWBox / 2 ? 1 : 0);
         if (i < pads.size() && onPick) {
            onPick(pads[i]);
            return;
         }
      }
      if (onClose)
         onClose();
   }
   std::function<bool(int)> used;
   std::function<int()> kit;
   std::function<void(int)> onPick;
   std::function<void()> onClose;
   float top = 150;
   static constexpr float kX = 30, kWBox = 300, kRowH = 21;

private:
   std::vector<int> free() const {
      std::vector<int> p;
      for (int pad = 0; pad < kPads; ++pad)
         if (!used(pad))
            p.push_back(pad);
      return p;
   }
   EditorHost &host_;
};

const std::vector<double> &defaults() {
   static const std::vector<double> d = defaultValues();
   return d;
}

// ---- the panel ----

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host)
      : DesignFrame(kW, kH), host_(host), bindings_(host),
        rng_(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {
      tips_.maxWidth = 260.0f;
      tips_.add(add<ui::HoverArea>({14, 12, 180, 40}), "JDrummer version " JCLAP_PLUGIN_VERSION);
      tabs_ = &add<ui::TabBar>({214, 18, 330, 30}, std::vector<std::string>{"GENERATE", "EDIT", "PLAY"});
      tabs_->setAccent(kOrange);
      tabs_->onChange = [this](int p) {
         if (p == host_.page)
            return;
         host_.page = p;
         closeOverlays();
         redraw();
      };
      header_ = &add<HeaderTransport>({564, 18, 92, 28});
      header_->onPlay = [this] { togglePlay(); };
      tips_.add(*header_, "PLAY runs JDrummer on its own while the host is stopped.");
      add<ui::UpdateNotice>({214, 50, 140, 12});
      buildPresets();
      buildGenerate();
      buildEdit();
      buildPlay();
      buildOverlays();
      tabs_->select(host_.page);
      showOverlays();
      if (host_.litter.empty())
         breed();
      startTimer(33);
   }
   ~Panel() override {
      if (host_.learnAction >= 0)
         host_.armLearn(false);
   }

   void showPage(int page) {
      host_.page = page;
      tabs_->select(page);
      showOverlays();
   }
   void showOverlays() {
      players_->setVisible(host_.playersOpen && host_.page == kGeneratePage);
      if (host_.browserOpen && host_.page == kGeneratePage)
         browser_->open();
      else
         browser_->setVisible(false);
      notes_->setVisible(host_.notesOpen && host_.page == kPlayPage);
      notesControls(host_.notesOpen && host_.page == kPlayPage);
      addPad_->top = 120 + grid_->addPadBottom() + 4;
      addPad_->setVisible(host_.addPadOpen && host_.page == kEditPage);
   }

   void timerCallback() override {
      const PlayFrame &f = host_.frame();
      frame_ = f;
      header_->frame = &frame_;
      header_->redraw();
      if (host_.page == kGeneratePage) {
         song_->frame = &frame_;
         song_->redraw();
      } else if (host_.page == kEditPage) {
         grid_->frame = &frame_;
         grid_->redraw();
         syncEditTop();
      } else {
         tiles_->frame = &frame_;
         tiles_->redraw();
         now_->frame = &frame_;
         now_->redraw();
         transport_->frame = &frame_;
         transport_->redraw();
         padMap_->update(frame_);
         notes_->frame = &frame_;
         if (notes_->isVisible())
            notes_->redraw();
      }
      if (host_.learnAction >= 0) {
         const int note = host_.takeLearned();
         if (note >= 0) {
            setParam(static_cast<clap_id>(kLearnNote + host_.learnAction), note);
            host_.learnAction = -1;
            notes_->redraw();
            redraw();
         }
      }
   }

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      c.setColor(t.background);
      c.fill(0, 0, width(), height());
      c.setColor(t.headerBar);
      c.fill(0, 0, width(), kHeader * s);
      // The logo: a drum and two sticks.
      c.setColor(kOrange);
      c.segment(22 * s, 13 * s, 30 * s, 24 * s, 2 * s, true);
      c.segment(44 * s, 13 * s, 36 * s, 24 * s, 2 * s, true);
      c.circle(19.5f * s, 10.5f * s, 5 * s);
      c.circle(41.5f * s, 10.5f * s, 5 * s);
      c.roundedRectangleBorder(17 * s, 24 * s, 32 * s, 22 * s, 9 * s, 2 * s);
      c.segment(18 * s, 33 * s, 48 * s, 33 * s, 1.6f * s, true);
      c.text("J", ui::font(28 * s), visage::Font::kLeft, 59 * s, 12 * s, 30 * s, 40 * s);
      c.setColor(t.text);
      c.text("Drummer", ui::font(28 * s), visage::Font::kLeft, 72 * s, 12 * s, 140 * s, 40 * s);
      if (host_.page == kGeneratePage)
         drawGenerate(c, s);
      else if (host_.page == kEditPage)
         drawEdit(c, s);
      else
         drawPlay(c, s);
   }

private:
   template <typename T, typename... Args>
   T &onPage(Page page, ui::Rect bounds, Args &&...args) {
      return tabs_->addToPage(page, add<T>(bounds, std::forward<Args>(args)...));
   }

   void setParam(clap_id id, double v) {
      host_.beginEdit(id);
      host_.setParam(id, v);
      host_.endEdit(id);
   }
   double param(clap_id id) const { return host_.paramValue(id); }
   int whole(clap_id id) const { return static_cast<int>(std::lround(host_.paramValue(id))); }
   Recipe recipe() const { return recipeFrom([this](clap_id id) { return host_.paramValue(id); }); }

   void togglePlay() { setParam(kPlay, whole(kPlay) ? 0.0 : 1.0); }

   // ---- making songs ----

   void remember() {
      host_.previous.push_back(host_.song());
      if (host_.previous.size() > 16)
         host_.previous.erase(host_.previous.begin());
   }
   void regenerate() {
      Song s = host_.song();
      generateSong(recipe(), s);
      host_.setSong(s);
      host_.parents.clear();
      breed();
      redraw();
   }
   void rollSong() {
      remember();
      setParam(kSeed, static_cast<double>(1 + rng_() % 99999));
      regenerate();
   }
   void previousRoll() {
      if (host_.previous.empty())
         return;
      host_.setSong(host_.previous.back());
      host_.previous.pop_back();
      host_.parents.clear();
      breed();
      redraw();
   }
   void pickStyle(int genre, int style) {
      for (const auto &[id, v] : styleValues(genre, style))
         setParam(id, v);
      if (whole(kBlendGenre) - 1 == genre)
         setParam(kBlendGenre, 0);
      remember();
      regenerate();
   }
   // Five children of the section's groove. The same section, generation and litter number give the
   // same five (so the panel opens on the litter it closed on); NEW LITTER asks for the next.
   void breed(bool fresh = false) {
      if (fresh)
         ++litterRolls_;
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      Rng rng(mix(mix(sec.seed, sec.generation), litterRolls_ * 31u + static_cast<uint32_t>(host_.mutateSection)));
      host_.litter.clear();
      host_.tags.clear();
      std::vector<int> used;
      const Genre &g = genreAt(whole(kGenre));
      for (int i = 0; i < 5; ++i) {
         std::string tag;
         host_.litter.push_back(mutate(sec.vars[0], g.kit, param(kMutation), rng, tag, &used, g.tradition));
         host_.tags.push_back(tag);
      }
      if (mutate_)
         mutate_->redraw();
   }
   void adopt(int child) {
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.mutateSection)];
      host_.parents.push_back(sec.vars[0]);
      adoptChild(recipe(), sec, host_.litter[static_cast<size_t>(child)], rng_());
      host_.setSong(s);
      breed();
      redraw();
   }
   void back() {
      if (host_.parents.empty())
         return;
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.mutateSection)];
      adoptChild(recipe(), sec, host_.parents.back(), rng_());
      sec.generation = static_cast<uint16_t>(std::max(0, sec.generation - 2));
      host_.parents.pop_back();
      host_.setSong(s);
      breed();
      redraw();
   }

   // ---- header ----

   void buildPresets() {
      presets_ = &add<ui::PresetBar>({664, 18, 322, 28}, factoryPresets(), kPresetFormat, host_);
      presets_->setAccent(kOrange);
      presets_->defaults = defaults();
      presets_->currentName = [this] { return host_.presetName(); };
      presets_->setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      presets_->onLoad = [this](const ui::Preset &p) {
         ui::applyPreset(p, host_, defaults());
         // A user preset has its song beside it; a factory preset is made from its recipe.
         Song song;
         const std::filesystem::path file = ui::userPresetFolder(kPresetFormat) / (p.name + kPresetFormat.extension);
         if (readSong(songFile(file), song)) {
            host_.setSong(song);
            host_.parents.clear();
            breed();
         } else {
            Song s;
            generateSong(recipe(), s);
            host_.setSong(s);
            host_.parents.clear();
            breed();
         }
         redraw();
      };
      presets_->onSaved = [this](const std::filesystem::path &file) { writeSong(songFile(file), host_.song()); };
      presets_->onDeleted = [](const std::filesystem::path &file) {
         std::error_code ec;
         std::filesystem::remove(songFile(file), ec);
      };
      presets_->onDice = [this] { rollSong(); };
      tips_.add(*presets_, "Every style is a preset, filed by family and genre. The die rolls the song again.");
   }

   void closeOverlays() {
      host_.playersOpen = host_.browserOpen = host_.notesOpen = host_.addPadOpen = false;
      if (host_.learnAction >= 0) {
         host_.learnAction = -1;
         host_.armLearn(false);
      }
      showOverlays();
   }

   // ---- GENERATE ----

   void buildGenerate() {
      tiles12_ = &onPage<GenreTiles>(kGeneratePage, {26, 104, 507, 140}, host_);
      tiles12_->onPick = [this](int g) {
         const int style = g == whole(kGenre) ? whole(kStyle) : 0;
         pickStyle(g, style);
      };
      tiles12_->onBlend = [this](int g) {
         setParam(kBlendGenre, whole(kBlendGenre) - 1 == g || g == whole(kGenre) ? 0 : g + 1);
         remember();
         regenerate();
      };
      tips_.add(*tiles12_, "Click a genre to hear it. Shift-click another to blend it in (BLEND sets how much).");
      style_ = &onPage<ui::Dropdown>(kGeneratePage, {250, 83, 150, 20});
      style_->setAccent(kOrange);
      style_->onChange = [this](int i) { pickStyle(whole(kGenre), i); };
      tips_.add(*style_, "The genre's styles. Each is a groove, a tempo and its twists.");
      auto &all = onPage<FlatButton>(kGeneratePage, {404, 83, 76, 20}, "ALL 34 GENRES", 8.5f);
      all.textColour = kOrange;
      all.onClick = [this](const visage::MouseEvent &) {
         host_.browserOpen = true;
         showOverlays();
      };
      blend_ = &onPage<ParamSlider>(kGeneratePage, {486, 90, 46, 8}, host_, kBlend, "", "", "", kPurple);
      blend_->onDone = [this] { regenerate(); };
      tips_.add(*blend_, "How much of the blended genre comes in.");

      playersRow_ = &onPage<PlayersRow>(kGeneratePage, {562, 104, 412, 26}, host_);
      playersRow_->onOpen = [this](bool) {
         host_.playersOpen = !host_.playersOpen;
         showOverlays();
      };
      playersRow_->onClearPerc = [this] {
         setParam(kPercussionist, 0);
         regenerate();
      };
      energy_ = &onPage<ParamSlider>(kGeneratePage, {562, 140, 250, 44}, host_, kEnergy, "ENERGY", "chill", "peak time", kOrange);
      energy_->onDone = [this] { regenerate(); };
      tips_.add(*energy_, "How hard the whole song goes. Each section's own energy (in THE SONG) rides on it.");
      weird_ = &onPage<ParamSlider>(kGeneratePage, {562, 196, 250, 44}, host_, kWeird, "WEIRD", "by the book", "out there", kPurple);
      weird_->onDone = [this] { regenerate(); };
      tips_.add(*weird_, "Hits off the grid, left to chance, rolled, stray sounds and odd lengths.");
      auto &roll = onPage<FlatButton>(kGeneratePage, {828, 142, 146, 52}, "ROLL SONG", 12.5f);
      roll.accent = roll.dice = true;
      roll.onClick = [this](const visage::MouseEvent &) { rollSong(); };
      tips_.add(roll, "A new seed for every section that isn't locked.");
      auto &prev = onPage<FlatButton>(kGeneratePage, {828, 202, 146, 38}, "PREVIOUS ROLL", 9.5f);
      prev.onClick = [this](const visage::MouseEvent &) { previousRoll(); };

      song_ = &onPage<SongStrip>(kGeneratePage, {26, 296, 948, 130}, host_);
      song_->onSelect = [this](int i) {
         host_.mutateSection = i;
         host_.parents.clear();
         breed();
         redraw();
      };
      song_->onDice = [this](int i) {
         remember();
         Song s = host_.song();
         rollSection(recipe(), s, i, rng_());
         host_.setSong(s);
         if (i == host_.mutateSection)
            breed();
         redraw();
      };
      song_->onLock = [this](int i) {
         Song s = host_.song();
         s.sections[static_cast<size_t>(i)].locked ^= 1;
         host_.setSong(s);
         redraw();
      };
      song_->onEnergy = [this](int i) {
         Song s = host_.song();
         rollSection(recipe(), s, i, s.sections[static_cast<size_t>(i)].seed);
         host_.setSong(s);
         if (i == host_.mutateSection)
            breed();
         redraw();
      };
      tips_.add(*song_, "Click a section to mutate it below. Drag its energy up or down (to 0 for silence); the dice "
                        "rolls just that section, the lock keeps it from ROLL SONG.");

      mutation_ = &onPage<ParamSlider>(kGeneratePage, {650, 462, 120, 8}, host_, kMutation, "", "", "", kPurple);
      mutation_->onDone = [this] { breed(); };
      auto &litter = onPage<FlatButton>(kGeneratePage, {836, 455, 138, 22}, "NEW LITTER", 9.5f);
      litter.dice = true;
      litter.onClick = [this](const visage::MouseEvent &) { breed(true); };
      mutate_ = &onPage<MutateRow>(kGeneratePage, {26, 486, 948, 112}, host_);
      mutate_->onPick = [this](int i) { adopt(i); };
      mutate_->onBack = [this] { back(); };
   }

   void drawGenerate(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 528, 180, "GENRE");
      label(c, s, "shift-click: blend", 88, 86, 150, kFaint, visage::Font::kLeft, 8.5f);
      const int blend = whole(kBlendGenre) - 1;
      if (blend >= 0)
         label(c, s, "+ " + std::string(genreAt(blend).name), 160, 86, 85, kPurple, visage::Font::kRight, 8.5f);
      drawCard(c, s, 548, 76, 440, 180, "HOW IT PLAYS");
      drawCard(c, s, 12, 264, 976, 170, "THE SONG",
               std::to_string(host_.song().totalBars()) +
                  " bars \xC2\xB7 click a section to mutate it below \xC2\xB7 lock what you like and ROLL SONG leaves it alone");
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      drawCard(c, s, 12, 442, 976, 164, std::string("MUTATE ") + kSectionCaps[host_.mutateSection]);
      label(c, s, "gen " + std::to_string(sec.generation + 1), 150, 452, 60, kPurple, visage::Font::kLeft, 9.5f);
      label(c, s, "MUTATION", 560, 455, 84, 0, visage::Font::kRight, 9.5f);
      label(c, s, "wild", 776, 455, 40, kFaint, visage::Font::kLeft, 8.5f);
      label(c, s, "Pick a genre and a player, roll the song, then mutate any section until it sits right. EDIT for the last "
                  "details.",
            12, 618, 976, 0, visage::Font::kRight, 8.5f);
      syncGenerate();
   }

   void syncGenerate() {
      const Genre &g = genreAt(whole(kGenre));
      std::vector<std::string> names;
      for (const Style &st : g.styles)
         names.push_back(st.name);
      if (names != style_->options())
         style_->setOptions(names);
      style_->setSelected(std::clamp(whole(kStyle), 0, static_cast<int>(names.size()) - 1));
      blend_->setVisible(host_.page == kGeneratePage && whole(kBlendGenre) > 0);
   }

   // ---- EDIT ----

   void buildEdit() {
      std::vector<std::string> sections;
      for (const char *n : kSectionCaps)
         sections.push_back(n);
      editSection_ = &onPage<ui::Dropdown>(kEditPage, {84, 86, 120, 22}, sections);
      editSection_->setAccent(kOrange);
      editSection_->onChange = [this](int i) {
         host_.editSection = i;
         redraw();
      };
      editVar_ = &onPage<ui::SegmentedSwitch>(kEditPage, {250, 86, 260, 22},
                                              std::vector<std::string>{"1", "2", "3", "4", "FILL", "PERC"});
      editVar_->setAccent(kOrange);
      editVar_->onChange = [this](int i) {
         host_.editVar = i;
         redraw();
      };
      tips_.add(*editVar_, "The section's four variations, its fill, and the percussionist's part.");
      steps_ = &onPage<ui::NumberBox>(kEditPage, {626, 86, 76, 22}, ui::ValueRange{1, kMaxSteps, 16, 1, false, true});
      steps_->setStepButtons(true);
      steps_->onChange = [this](double v) {
         Song s = host_.song();
         EditGrid::pick(s, host_.editSection, host_.editVar).steps = static_cast<uint8_t>(std::lround(v));
         EditGrid::edited(s, host_.editSection, host_.editVar);
         host_.setSong(s);
         redraw();
      };
      rate_ = &onPage<ui::Dropdown>(kEditPage, {744, 86, 80, 22},
                                    std::vector<std::string>{kRateNames[0], kRateNames[1], kRateNames[2], kRateNames[3]});
      rate_->setAccent(kOrange);
      rate_->onChange = [this](int i) {
         Song s = host_.song();
         EditGrid::pick(s, host_.editSection, host_.editVar).rate = static_cast<uint8_t>(i);
         EditGrid::edited(s, host_.editSection, host_.editVar);
         host_.setSong(s);
         redraw();
      };
      auto &dice = onPage<FlatButton>(kEditPage, {840, 86, 134, 22}, "DICE", 9.5f);
      dice.dice = true;
      dice.onClick = [this](const visage::MouseEvent &) {
         Song s = host_.song();
         Pattern &p = EditGrid::pick(s, host_.editSection, host_.editVar);
         Rng rng(rng_());
         std::string tag;
         const bool perc = host_.editVar == kVariations + 1;
         const int who = std::max(0, whole(kPercussionist) - 1);
         p = mutate(p, perc ? percussionistKit(who) : genreAt(whole(kGenre)).kit, param(kMutation), rng, tag, nullptr,
                    perc ? percussionistTradition(who) : genreAt(whole(kGenre)).tradition);
         EditGrid::edited(s, host_.editSection, host_.editVar);
         host_.setSong(s);
         diceTag_ = tag;
         redraw();
      };
      tips_.add(dice, "Mutates this pattern once, by MUTATION. Again for another.");
      grid_ = &onPage<EditGrid>(kEditPage, {26, 120, 948, EditGrid::kGridH}, host_);
      grid_->onAddPad = [this] {
         host_.addPadOpen = true;
         showOverlays();
      };
      grid_->onChange = [this] { diceTag_.clear(); };
      grid_->onHitMenu = [this](int pad, int step, visage::Point at) { hitMenu(pad, step, at); };
   }

   void hitMenu(int pad, int step, visage::Point at) {
      visage::PopupMenu m;
      m.addOption(1, "Accent");
      m.addOption(2, "Normal");
      m.addOption(3, "Ghost");
      m.addBreak();
      for (int r = 1; r <= kMaxRatchet; ++r)
         m.addOption(10 + r, r == 1 ? "No ratchet" : "Ratchet " + std::to_string(r));
      m.addBreak();
      for (int c : {100, 75, 50, 25})
         m.addOption(100 + c, "Chance " + std::to_string(c) + "%");
      m.onSelection() += [this, pad, step](int id) {
         Song s = host_.song();
         Hit &h = EditGrid::pick(s, host_.editSection, host_.editVar).at(pad, step);
         if (id == 1)
            h.vel = kAccent;
         else if (id == 2)
            h.vel = kNormal;
         else if (id == 3)
            h.vel = kGhost;
         else if (id > 10 && id <= 10 + kMaxRatchet)
            h.ratchet = static_cast<uint8_t>(id - 10);
         else if (id >= 100)
            h.chance = static_cast<uint8_t>(id - 100);
         EditGrid::edited(s, host_.editSection, host_.editVar);
         host_.setSong(s);
         grid_->redraw();
      };
      m.show(grid_, at);
   }

   void syncEditTop() {
      editSection_->setSelected(host_.editSection);
      editVar_->setSelected(host_.editVar);
      const Pattern &p = EditGrid::pick(const_cast<Song &>(host_.song()), host_.editSection, host_.editVar);
      steps_->setValue(p.steps);
      rate_->setSelected(p.rate);
   }

   void drawEdit(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 976, 534, "");
      label(c, s, "SECTION", 26, 89, 60, 0, visage::Font::kLeft, 10);
      label(c, s, "VAR", 220, 89, 30, 0, visage::Font::kLeft, 10);
      label(c, s, "STEPS", 580, 89, 44, 0, visage::Font::kLeft, 10);
      label(c, s, "RATE", 710, 89, 34, 0, visage::Font::kLeft, 10);
      if (!diceTag_.empty())
         label(c, s, "dice: " + diceTag_, 520, 112, 450, kPurple, visage::Font::kRight, 8.5f);
      label(c, s, "Click a cell to add or remove a hit, drag up or down for velocity, right-click for accent, ratchet and "
                  "chance.",
            12, 618, 976, 0, visage::Font::kRight, 8.5f);
      syncEditTop();
   }

   // ---- PLAY ----

   void buildPlay() {
      tiles_ = &onPage<SectionTiles>(kPlayPage, {26, 104, 948, 150}, host_);
      tiles_->onAction = [this](int a) {
         if (a <= kActOutro && whole(kSwitchBy) != 1)
            setParam(kSection, a);
         else if (a >= kActVar1 && a <= kActVar4 && whole(kSwitchBy) != 1)
            setParam(kVariation, a - kActVar1);
         if (a == kActFill || whole(kSwitchBy) == 1)
            host_.request(a);
      };
      tips_.add(*tiles_, "Click a section to switch to it (at SWITCH AT), a number for that variation, FILL for a fill.");
      auto &switchAt = onPage<ui::Dropdown>(kPlayPage, {600, 84, 104, 20}, namesOf(kSwitchAt));
      bindings_.bind(switchAt, kSwitchAt);
      auto &by = onPage<ui::Dropdown>(kPlayPage, {738, 84, 112, 20}, namesOf(kSwitchBy));
      bindings_.bind(by, kSwitchBy);
      tips_.add(by, "What switches sections: a note, the Section parameter (and a click here), or both.");
      notesButton_ = &onPage<FlatButton>(kPlayPage, {860, 82, 114, 24}, "NOTES", 9.5f);
      notesButton_->onClick = [this](const visage::MouseEvent &) {
         host_.notesOpen = !host_.notesOpen;
         showOverlays();
         redraw();
      };

      now_ = &onPage<NowPlaying>(kPlayPage, {26, 302, 592, 160}, host_);

      auto knob = [this](ui::Rect r, clap_id id, const char *name) -> ui::Knob & {
         auto &k = onPage<ui::Knob>(kPlayPage, r, rangeOf(id));
         k.setLabel(name);
         k.valueText = [id](double v) {
            char b[32];
            formatParam(id, v, b, sizeof(b));
            return std::string(b);
         };
         bindings_.bind(k, id);
         return k;
      };
      tips_.add(knob({648, 298, 66, 84}, kVary, "VARY"), "Each bar may take another variation, and hits come and go.");
      tips_.add(knob({716, 298, 66, 84}, kGhosts, "GHOSTS"), "Soft snare hits around the beat.");
      tips_.add(knob({784, 298, 66, 84}, kSwing, "SWING"), "Every second sixteenth late (or eighth: SWING IN).");
      auto &feel = onPage<ui::SegmentedSwitch>(kPlayPage, {862, 362, 112, 20}, std::vector<std::string>{"16THS", "8THS"});
      feel.setAccent(kOrange);
      bindings_.bind(feel, kSwingFeel);
      tips_.add(feel, "What SWING swings: the sixteenths, or the eighths (a shuffle; the sixteenths move with them).");
      auto &dice = onPage<ui::SegmentedSwitch>(kPlayPage, {652, 408, 322, 20},
                                               std::vector<std::string>{"NEVER", "1", "2", "4 BARS", "8"});
      dice.setAccent(kOrange);
      bindings_.bind(dice, kSameDice);
      tips_.add(dice, "The dice repeat every so many bars, so the chance hits make a pattern of their own.");
      auto &fills = onPage<ui::SegmentedSwitch>(kPlayPage, {652, 448, 214, 20},
                                                std::vector<std::string>{"OFF", "EVERY 4", "EVERY 8", "16"});
      fills.setAccent(kOrange);
      bindings_.bind(fills, kAutoFill);
      auto &song = onPage<ui::SegmentedSwitch>(kPlayPage, {862, 316, 112, 20}, std::vector<std::string>{"LOOP", "SONG"});
      song.setAccent(kOrange);
      bindings_.bind(song, kSongMode);
      tips_.add(song, "LOOP stays in a section; SONG plays each for its bars, then the next.");

      transport_ = &onPage<TransportView>(kPlayPage, {26, 508, 260, 80}, host_);
      transport_->onPlay = [this] { togglePlay(); };
      auto &sync = onPage<ui::SegmentedSwitch>(kPlayPage, {290, 528, 186, 20}, std::vector<std::string>{"AUTO", "HOST", "FREE"});
      sync.setAccent(kOrange);
      bindings_.bind(sync, kSync);
      tips_.add(sync, "AUTO follows the host while it plays, and runs free on PLAY while it's stopped.");
      auto &tempo = onPage<ui::NumberBox>(kPlayPage, {290, 574, 96, 20}, rangeOf(kFreeTempo, 1.0));
      tempo.setStepButtons(true);
      tempo.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(tempo, kFreeTempo);

      auto &kit = onPage<ui::Dropdown>(kPlayPage, {510, 528, 118, 20}, namesOf(kKit));
      bindings_.bind(kit, kKit);
      tips_.add(kit, "The kit JKit has loaded: it names the lanes. Set JKit's own KIT to match.");
      auto &channel = onPage<ui::NumberBox>(kPlayPage, {510, 574, 54, 20}, rangeOf(kChannel, 1.0));
      channel.setStepButtons(true);
      channel.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(channel, kChannel);
      auto &perc = onPage<ui::NumberBox>(kPlayPage, {574, 574, 54, 20}, rangeOf(kPercChannel, 1.0));
      perc.setStepButtons(true);
      perc.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(perc, kPercChannel);
      tips_.add(perc, "The percussionist's channel, for a second JKit set to PERCUSSION.");
      padMap_ = &onPage<PadMap>(kPlayPage, {642, 504, 334, 96}, host_);
   }

   static std::vector<std::string> namesOf(clap_id id) {
      int count = 0;
      const char *const *names = choiceNames(id, count);
      std::vector<std::string> out;
      for (int i = 0; i < count; ++i)
         out.push_back(upper(names[i]));
      return out;
   }
   static ui::ValueRange rangeOf(clap_id id, double step = 0.0) {
      return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, false,
              (kParams[id].flags & CLAP_PARAM_IS_STEPPED) != 0};
   }

   void drawPlay(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 976, 188, "SECTIONS");
      label(c, s, "SWITCH AT", 520, 87, 76, 0, visage::Font::kRight, 9.5f);
      label(c, s, "BY", 708, 87, 26, 0, visage::Font::kRight, 9.5f);
      const Section &sec = host_.song().sections[static_cast<size_t>(std::clamp(frame_.section, 0, 6))];
      const Pattern &p = frame_.fill ? sec.fill : sec.vars[static_cast<size_t>(std::clamp(frame_.variation, 0, 3))];
      char note[128];
      std::snprintf(note, sizeof(note), "%s, %s \xC2\xB7 %d of JKit's 16 pads", kSectionNames[std::clamp(frame_.section, 0, 6)],
                    frame_.fill ? "fill" : ("variation " + std::to_string(frame_.variation + 1)).c_str(), p.padsUsed());
      drawCard(c, s, 12, 272, 620, 198, "NOW PLAYING", note);
      drawCard(c, s, 640, 272, 348, 198, "LIFE", "each pass a little different");
      label(c, s, "SAME DICE EVERY", 652, 392, 200, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "AUTO FILLS", 652, 432, 200, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "PLAY", 862, 298, 112, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "SWING IN", 862, 344, 112, 0, visage::Font::kLeft, 9.5f);
      drawCard(c, s, 12, 478, 476, 128, "TRANSPORT", frame_.hostPlaying ? "host playing" : "host stopped");
      label(c, s, "SYNC", 290, 512, 100, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "TEMPO WHEN FREE", 290, 558, 160, 0, visage::Font::kLeft, 9.5f);
      char out[64];
      std::snprintf(out, sizeof(out), "MIDI to JKit, notes C2-D#3, ch %d", whole(kChannel));
      drawCard(c, s, 496, 478, 492, 128, "OUTPUT", out);
      label(c, s, "KIT", 510, 512, 100, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "CHANNEL", 510, 558, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "PERC", 574, 558, 60, 0, visage::Font::kLeft, 9.5f);
      notesButton_->lit = host_.notesOpen;
      notesButton_->outline = host_.notesOpen ? kOrange : 0;
      const char *hint = whole(kSwitchBy) == 1 ? "Play a section's note to switch to it."
                                               : "Click a section, play its note, or automate the Section parameter.";
      label(c, s, std::string(hint) + " It changes at the " + upper(namesOf(kSwitchAt)[static_cast<size_t>(whole(kSwitchAt))]) + ".",
            12, 618, 976, 0, visage::Font::kRight, 8.5f);
   }

   // ---- overlays ----

   void buildOverlays() {
      players_ = &add<PlayersOverlay>({0, 0, kW, kH}, host_);
      players_->onPick = [this](int i) {
         if (i < 6)
            setParam(kPlayer, i);
         else
            setParam(kPercussionist, whole(kPercussionist) == i - 5 ? 0 : i - 5);
         regenerate();
      };
      players_->onClose = [this] { closeOverlays(); };
      players_->setOnTop(true);
      players_->setVisible(false);
      browser_ = &add<BrowserOverlay>({0, 0, kW, kH}, host_);
      browser_->onPick = [this](int g, int st) {
         pickStyle(g, st);
         closeOverlays();
      };
      browser_->onClose = [this] { closeOverlays(); };
      browser_->setOnTop(true);
      browser_->setVisible(false);
      addPad_ = &add<AddPadMenu>({0, 0, kW, kH}, host_);
      addPad_->used = [this](int pad) {
         return grid_->pattern().uses(pad) || (host_.added >> pad & 1);
      };
      addPad_->kit = [this] { return grid_->kit(); };
      addPad_->onPick = [this](int pad) {
         host_.added = static_cast<uint16_t>(host_.added | (1u << pad));
         host_.editPad = pad;
         closeOverlays();
         redraw();
      };
      addPad_->onClose = [this] { closeOverlays(); };
      addPad_->setOnTop(true);
      addPad_->setVisible(false);

      const ui::Rect nr{348, 110, 640, 290};
      notes_ = &add<NotesOverlay>(nr, host_);
      notes_->setOnTop(true);
      notes_->onLearn = [this](int a) {
         host_.learnAction = a;
         host_.armLearn(a >= 0);
         notes_->redraw();
      };
      // The notes card's own controls, on top of it.
      map_ = &add<ui::SegmentedSwitch>({nr.x + 380, nr.y + 10, 244, 20}, std::vector<std::string>{"KEYBOARD MAP", "CUSTOM"});
      map_->setAccent(kOrange);
      bindings_.bind(*map_, kNoteMap);
      map_->onChange = [this, old = map_->onChange](int i) {
         if (old)
            old(i);
         notes_->redraw();
      };
      octave_ = &add<ui::NumberBox>({nr.x + 16, nr.y + 228, 90, 20}, rangeOf(kNoteOctave, 1.0));
      octave_->setStepButtons(true);
      octave_->valueText = [](double v) { return "OCT " + std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(*octave_, kNoteOctave);
      noteChannel_ = &add<ui::NumberBox>({nr.x + 116, nr.y + 228, 74, 20}, rangeOf(kNoteChannel, 1.0));
      noteChannel_->setStepButtons(true);
      noteChannel_->valueText = [](double v) {
         return v < 0.5 ? std::string("ANY") : "CH " + std::to_string(static_cast<int>(std::lround(v)));
      };
      bindings_.bind(*noteChannel_, kNoteChannel);
      reset_ = &add<FlatButton>({nr.x + 200, nr.y + 228, 80, 20}, "RESET ALL", 8.5f);
      reset_->onClick = [this](const visage::MouseEvent &) {
         for (int a = 0; a < kActionCount; ++a)
            setParam(static_cast<clap_id>(kLearnNote + a), 12 + kActionKey[a]);
         notes_->redraw();
      };
      const clap_id boxes[4] = {kFillInto, kVelEnergy, kHeldOnly, kPassNotes};
      const char *names[4] = {"Fill into the new section", "Velocity sets energy", "Play only while held",
                              "Pass other notes to JKit"};
      for (int i = 0; i < 4; ++i) {
         auto &b = add<CheckBox>({nr.x + 300 + (i % 2) * 168, nr.y + 228 + (i / 2) * 22, 166, 18}, names[i]);
         const clap_id id = boxes[i];
         b.onToggle = [this, id](bool on) { setParam(id, on ? 1.0 : 0.0); };
         checks_[i] = &b;
      }
      notesControls(false);
   }

   void notesControls(bool show) {
      for (visage::Frame *f : {static_cast<visage::Frame *>(map_), static_cast<visage::Frame *>(octave_),
                               static_cast<visage::Frame *>(noteChannel_), static_cast<visage::Frame *>(reset_)}) {
         f->setOnTop(true);
         f->setVisible(show);
      }
      const clap_id boxes[4] = {kFillInto, kVelEnergy, kHeldOnly, kPassNotes};
      for (int i = 0; i < 4; ++i) {
         checks_[i]->on = whole(boxes[i]) != 0;
         checks_[i]->setOnTop(true);
         checks_[i]->setVisible(show);
      }
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   std::mt19937 rng_;
   uint32_t litterRolls_ = 0;
   PlayFrame frame_;
   ui::TabBar *tabs_ = nullptr;
   ui::PresetBar *presets_ = nullptr;
   HeaderTransport *header_ = nullptr;
   GenreTiles *tiles12_ = nullptr;
   ui::Dropdown *style_ = nullptr;
   ParamSlider *blend_ = nullptr, *energy_ = nullptr, *weird_ = nullptr, *mutation_ = nullptr;
   PlayersRow *playersRow_ = nullptr;
   SongStrip *song_ = nullptr;
   MutateRow *mutate_ = nullptr;
   ui::Dropdown *editSection_ = nullptr, *rate_ = nullptr;
   ui::SegmentedSwitch *editVar_ = nullptr;
   ui::NumberBox *steps_ = nullptr;
   EditGrid *grid_ = nullptr;
   std::string diceTag_;
   SectionTiles *tiles_ = nullptr;
   NowPlaying *now_ = nullptr;
   TransportView *transport_ = nullptr;
   PadMap *padMap_ = nullptr;
   FlatButton *notesButton_ = nullptr;
   PlayersOverlay *players_ = nullptr;
   BrowserOverlay *browser_ = nullptr;
   AddPadMenu *addPad_ = nullptr;
   NotesOverlay *notes_ = nullptr;
   ui::SegmentedSwitch *map_ = nullptr;
   ui::NumberBox *octave_ = nullptr, *noteChannel_ = nullptr;
   FlatButton *reset_ = nullptr;
   CheckBox *checks_[4] = {};
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host), ui::WindowOptions{kEditorWidth, kEditorHeight});
}

bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.showPage(page);
   panel.timerCallback();
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jdrummer
