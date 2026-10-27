// JSynth's panel: the synth's panels on the left, the sky beside them drawn with bloom, and the
// voice and output under it. SKY hides the panels and lets the sky fill the window. 1648 x 940 design units, scaled to the window.
//
// Every held note is a creature. The panel flies both species' flocks itself, about 60 frames a
// second on the main thread, from the held notes and the flock params, and each creature sheds ink
// that blooms and fades behind it. It is a mock: the audio thread doesn't fly them yet, so nothing
// here reaches the sound.
//
// MAP mode is how modulation is made: touch a source (a chip in the header or under the sky), which turns MAP on with it armed, then drag any knob to set how much that source moves it. While it is armed, its
// routes show as rings in its colour around the knobs they move; outside MAP the knobs show none.
// While a part plays, a knob its routes move shows, MAP or not, a white line on its outer ring out to
// where the part's newest voice has it now, and the drawings draw that voice's values.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>
#include <jclap/ui/xy_pad.h>

#include <visage/app.h>
#include <visage_graphics/post_effects.h>
#include <visage_ui/popup_menu.h>
#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "dsp.h"
#include "flock.h"
#include "params.h"
#include "presets.h"

namespace jsynth {

void EditorHost::setParams(const double *values) {
   for (size_t i = 0; i < paramCount(); ++i) {
      const clap_id id = kParams()[i].def.id;
      if (paramValue(id) == values[i])
         continue;
      beginEdit(id);
      setParam(id, values[i]);
      endEdit(id);
   }
}

namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1648.0f;
constexpr float kDesignHeight = 940.0f;
constexpr float kHeaderHeight = 52.0f;
constexpr float kPi = 3.14159265f;

// The sky the flocks fly in, right of the synth's panels (design units). The flock is simulated in
// sky units: y runs 0..1 down the sky's height and x 0..kSkyAspect across it.
constexpr float kSkyX = 912.0f, kSkyY = 60.0f, kSkyW = 720.0f, kSkyH = 540.0f;
static_assert(kSkyW / kSkyH == kSkyAspect, "the panel's sky has the flock's shape");

constexpr unsigned kText = 0xffe6e8eb;
constexpr unsigned kDim = 0xff9aa3b5;
constexpr unsigned kMapColor = 0xffff6fd2;
constexpr unsigned kCaughtColor = 0xffff4f8b;
// Part A is warm, part B cool; each creature takes one of its species' four inks.
constexpr unsigned kPartColors[kPartCount] = {0xfff28c28, 0xff6fb8ff};
constexpr unsigned kInks[kPartCount][4] = {{0xffffa040, 0xffff6a4a, 0xffffd060, 0xffff5fb0},
                                           {0xff5fd8ff, 0xff6f8cff, 0xffa98bff, 0xff4fe0c0}};
constexpr unsigned kOscColors[kOscCount] = {0xff6fe0ff, 0xffffb05a, 0xffa98bff};

visage::Color glow(unsigned argb, float alpha, float hdr) { return visage::Color(argb, hdr).withAlpha(alpha); }

// Each route source's colour, for its chip and the rings it draws on knobs.
unsigned sourceColor(int source) {
   static const unsigned colors[kSourceCount] = {
       0xff808080,                                                 // Off
       0xffffd36b, 0xffffa94d, 0xffff7a59,                         // Env 1-3
       0xff5fd3b0, 0xffc9a0ff,                                     // LFO 1, LFO 2
       0xfff6f0a0, 0xffff6fd2, 0xffb98bff, 0xff8fe8a8, 0xff9ad0c0, // Velocity, Press, Glide, Slide, Release
       0xffc0c8d8,                                                 // Key
       0xfff2c94c, 0xff6fe0ff, 0xffa98bff, 0xffff8a7a,             // Speed, Nearest, Heading, Turn
       0xffd0d0d0, 0xffe0e0ff,                                     // Random, Mod Wheel
       0xfff28c28, 0xff8fe8a8, 0xff6fb8ff, 0xffff6fd2};            // Macros
   return colors[std::clamp(source, 0, kSourceCount - 1)];
}
// The wingbeat's and the wind's colour, on the MOTION tab.
constexpr unsigned kMotionColors[kMotionCount] = {0xff7ee0ff, 0xff8fb8ff};
// Each touch's colour on the TOUCH tab: its source's.
unsigned touchColor(int touch) { return sourceColor(kSourceVelocity + touch); }

std::string text(clap_id id, double value) {
   char buffer[64];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0) {
   const jclap::core::ParamDef &d = findParam(id)->def;
   return {d.min, d.max, d.defaultValue, step, d.min < 0.0, (d.flags & CLAP_PARAM_IS_STEPPED) != 0};
}

std::string upper(std::string s) {
   for (char &ch : s)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
   return s;
}
std::vector<std::string> upper(const std::vector<std::string> &names) {
   std::vector<std::string> out;
   for (const std::string &n : names)
      out.push_back(upper(n));
   return out;
}

// Knobs, buttons and switches over the sky: darker, see-through tracks.
const ui::Theme &glassTheme() {
   static const ui::Theme theme = [] {
      ui::Theme t;
      t.track = 0x40ffffff;
      t.knobLight = 0xff3a4152;
      t.knobDark = 0xff141823;
      t.dimText = kDim;
      t.button = 0x40141a26;
      t.buttonHover = 0x60283040;
      t.panel = 0x80141a26;
      t.panelBorder = 0x40ffffff;
      return t;
   }();
   return theme;
}

// A translucent card with a title, for controls floating over the sky.
class Glass : public visage::Frame {
public:
   explicit Glass(std::string title = {}, unsigned accent = kDim) : title_(std::move(title)), accent_(accent) {
      setIgnoresMouseEvents(true, false);
   }
   float designHeight = 1.0f;

   void draw(visage::Canvas &c) override {
      const float s = height() / designHeight;
      c.setColor(visage::Color(0xb80b0e17));
      c.roundedRectangle(0, 0, width(), height(), 10 * s);
      c.setColor(visage::Color(0x2effffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * s, 1.0f * s);
      if (!title_.empty()) {
         c.setColor(accent_);
         c.text(title_, ui::font(13 * s), visage::Font::kLeft, 14 * s, 7 * s, width(), 18 * s);
      }
   }

private:
   std::string title_;
   unsigned accent_;
};

// What MAP mode needs from the panel. Knobs ask it for their rings and hand it their drags.
class Mapper {
public:
   virtual ~Mapper() = default;
   // MAP is on.
   virtual bool mapping() const = 0;
   // The source MAP mode is drawing routes from, or kSourceOff.
   virtual int armedSource() const = 0;

   struct Ring {
      int route, source;
      double amount;
   };
   // The routes in `part` to destination `dest` (an index into destinations()), in route order.
   virtual void rings(int part, int dest, std::vector<Ring> &out) const = 0;
   // The route from the armed source to `dest`, made if there is none (-1 when all are taken).
   virtual int routeFor(int part, int dest) = 0;
   virtual double routeAmount(int part, int route) const = 0;
   virtual void beginRoute(int part, int route) = 0;
   virtual void setRouteAmount(int part, int route, double amount) = 0;
   // Ends the drag; a route left at zero is removed.
   virtual void endRoute(int part, int route) = 0;
   // Removes the armed source's routes to `dest`, or every route to it with nothing armed.
   virtual void unmap(int part, int dest) = 0;
};

// A knob MAP mode can map. With a source armed, dragging it sets that route's amount instead of the
// knob's value, a double-click flips the route's sign and a right-click removes it. Only in MAP mode
// with a source armed does it show a route: a ring outside the knob's own, in the source's colour,
// spanning what it adds.
class MapKnob : public ui::Knob {
public:
   MapKnob(ui::ValueRange range, Mapper &mapper, int part, clap_id offset)
       : Knob(range), mapper_(mapper), part_(part), dest_(destinationIndex(offset)) {}

   int part() const { return part_; }
   // Its index in destinations(), or -1.
   int destination() const { return dest_; }

   void draw(visage::Canvas &c) override {
      Knob::draw(c);
      const int armed = mapper_.armedSource();
      if (dest_ < 0 || !mapper_.mapping() || armed == kSourceOff)
         return;
      rings_.clear();
      mapper_.rings(part_, dest_, rings_);
      const float w = width(), h = height(), s = h / 100.0f;
      const float d = std::min(w * 0.7f, 48.0f * s);
      const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;
      const float rd = d + 13.0f * s;
      const float sweep = 0.75f * kPi;
      const float from = fraction(value());
      c.setColor(glow(sourceColor(armed), 0.55f, 1.0f));
      c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 1.4f * s, -0.5f * kPi, sweep, true);
      for (const Mapper::Ring &r : rings_) {
         if (r.source != armed)
            continue;
         const float to = std::clamp(from + static_cast<float>(r.amount), 0.0f, 1.0f);
         if (std::fabs(to - from) > 0.002f) {
            const float a0 = -sweep + 2.0f * sweep * from, a1 = -sweep + 2.0f * sweep * to;
            c.setColor(glow(sourceColor(armed), 1.0f, 1.6f));
            c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 3.4f * s, 0.5f * (a0 + a1) - 0.5f * kPi,
                  0.5f * std::fabs(a1 - a0), true);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (!mapper_.mapping() || dest_ < 0)
         return Knob::mouseDown(e);
      if (e.isRightButton()) {
         mapper_.unmap(part_, dest_);
         redraw();
         return;
      }
      if (mapper_.armedSource() == kSourceOff)
         return;
      route_ = mapper_.routeFor(part_, dest_);
      if (route_ < 0)
         return;
      mapper_.beginRoute(part_, route_);
      start_ = mapper_.routeAmount(part_, route_);
      if (e.repeatClickCount() == 2)
         mapper_.setRouteAmount(part_, route_, start_ = -start_);
      startY_ = e.position.y;
      redraw();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return Knob::mouseDrag(e);
      const double amount = std::clamp(start_ + (startY_ - e.position.y) / (2.0f * height()), -1.0, 1.0);
      mapper_.setRouteAmount(part_, route_, amount);
      redraw();
   }
   void mouseUp(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return Knob::mouseUp(e);
      mapper_.endRoute(part_, route_);
      route_ = -1;
      redraw();
   }

private:
   Mapper &mapper_;
   int part_;
   int dest_;
   int route_ = -1;
   double start_ = 0.0;
   float startY_ = 0.0f;
   std::vector<Mapper::Ring> rings_;
};

// A value box MAP mode can map, for a param with no room for a knob: as a MapKnob, but its route
// shows as a bar along the bottom, from the value to where the source takes it.
class MapBox : public ui::NumberBox {
public:
   MapBox(ui::ValueRange range, Mapper &mapper, int part, clap_id offset)
       : NumberBox(range), mapper_(mapper), part_(part), dest_(destinationIndex(offset)) {}

   void draw(visage::Canvas &c) override {
      NumberBox::draw(c);
      const int armed = mapper_.armedSource();
      if (dest_ < 0 || !mapper_.mapping() || armed == kSourceOff)
         return;
      const float w = width(), h = height(), inset = h * 0.18f, line = std::max(1.5f, h * 0.08f);
      c.setColor(glow(sourceColor(armed), 0.55f, 1.0f));
      c.roundedRectangleBorder(0, 0, w, h, h * 0.18f, 1.4f);
      rings_.clear();
      mapper_.rings(part_, dest_, rings_);
      const float from = fraction(value()), span = w - 2 * inset;
      for (const Mapper::Ring &r : rings_) {
         if (r.source != armed)
            continue;
         const float to = std::clamp(from + static_cast<float>(r.amount), 0.0f, 1.0f);
         c.setColor(glow(sourceColor(armed), 1.0f, 1.6f));
         c.fill(inset + span * std::min(from, to), h - line - 1.0f, std::max(line, span * std::fabs(to - from)), line);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (!mapper_.mapping() || dest_ < 0)
         return NumberBox::mouseDown(e);
      if (e.isRightButton()) {
         mapper_.unmap(part_, dest_);
         redraw();
         return;
      }
      if (mapper_.armedSource() == kSourceOff)
         return;
      route_ = mapper_.routeFor(part_, dest_);
      if (route_ < 0)
         return;
      mapper_.beginRoute(part_, route_);
      start_ = mapper_.routeAmount(part_, route_);
      if (e.repeatClickCount() == 2)
         mapper_.setRouteAmount(part_, route_, start_ = -start_);
      startY_ = e.position.y;
      redraw();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return NumberBox::mouseDrag(e);
      mapper_.setRouteAmount(part_, route_, std::clamp(start_ + (startY_ - e.position.y) / dragLength(), -1.0, 1.0));
      redraw();
   }
   void mouseUp(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return NumberBox::mouseUp(e);
      mapper_.endRoute(part_, route_);
      route_ = -1;
      redraw();
   }

private:
   Mapper &mapper_;
   int part_;
   int dest_;
   int route_ = -1;
   double start_ = 0.0;
   float startY_ = 0.0f;
   std::vector<Mapper::Ring> rings_;
};

// A modulation source you can touch in MAP mode: a pill with its name, filled as far as the source
// is now. In MAP mode it pulses in its colour; armed, it is lit.
class SourceChip : public visage::Frame {
public:
   SourceChip(int source, std::string label) : source_(source), label_(std::move(label)) {}

   int source() const { return source_; }
   void setState(bool mapping, bool armed, float pulse) {
      if (mapping != mapping_ || armed != armed_ || (mapping && std::fabs(pulse - pulse_) > 0.02f)) {
         mapping_ = mapping;
         armed_ = armed;
         pulse_ = pulse;
         redraw();
      }
   }
   // How far the source is now, 0..1: it fills the chip and lights the LED. A bipolar source (an
   // LFO) passes its -1..1 swing as 0..1, and lights the LED by how far it is from the middle.
   void setLevel(float level, bool bipolar = false) {
      level = std::clamp(level, 0.0f, 1.0f);
      const float led = bipolar ? std::fabs(2.0f * level - 1.0f) : level;
      if (std::fabs(level - level_) > 0.004f || std::fabs(led - led_) > 0.004f) {
         level_ = level;
         led_ = led;
         redraw();
      }
   }
   std::function<void(int)> onClick;

   void mouseDown(const visage::MouseEvent &) override {
      if (onClick)
         onClick(source_);
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 22.0f, r = h * 0.5f;
      const unsigned color = sourceColor(source_);
      c.setColor(visage::Color(armed_ ? 0xe0161a26 : 0xa0101420));
      c.roundedRectangle(0, 0, w, h, r);
      if (armed_) {
         c.setColor(glow(color, 0.3f, 1.0f));
         c.roundedRectangle(0, 0, w, h, r);
      }
      if (level_ > 0.0f) {
         c.setColor(glow(color, armed_ ? 0.35f : 0.22f, 1.0f));
         c.roundedRectangle(0, 0, std::max(h, w * level_), h, r);
      }
      const float border = armed_ ? 1.0f : mapping_ ? 0.35f + 0.5f * pulse_ : 0.0f;
      if (border > 0.0f) {
         c.setColor(glow(color, border, armed_ ? 1.8f : 1.2f));
         c.roundedRectangleBorder(0, 0, w, h, r, (armed_ ? 1.6f : 1.2f) * s);
      }
      else {
         c.setColor(visage::Color(0x26ffffff));
         c.roundedRectangleBorder(0, 0, w, h, r, 1.0f * s);
      }
      // The LED: dim when the source is at rest, glowing as it rises.
      const float lx = 9 * s, ly = h * 0.5f, halo = 9 * s;
      c.setColor(visage::Brush::radial(glow(color, 0.7f * led_, 1.0f), glow(color, 0.0f, 1.0f), visage::Point(lx, ly), halo));
      c.circle(lx - halo, ly - halo, 2 * halo);
      c.setColor(visage::Color(color).interpolateWith(0xff20232c, 0.75f * (1.0f - led_)));
      c.circle(lx - 3.5f * s, ly - 3.5f * s, 7 * s);
      c.setColor(glow(0xffffffff, 0.8f * led_ * led_, 1.0f));
      c.circle(lx - 1.5f * s, ly - 1.5f * s, 3 * s);
      c.setColor(armed_ ? 0xffffffff : kText);
      c.text(label_, ui::font(10 * s), visage::Font::kLeft, 16 * s, 0, w - 17 * s, h);
   }

private:
   int source_;
   std::string label_;
   bool mapping_ = false, armed_ = false;
   float pulse_ = 0.0f, level_ = 0.0f, led_ = 0.0f;
};

// The header's preset bar: arrows step through the presets, the name opens a menu of them by
// category (and the user's own), and SAVE names the current sound and saves it as a user preset.
class PresetBar : public visage::Frame {
public:
   explicit PresetBar(EditorHost &host)
      : host_(host), favorites_(userPresetFolder().parent_path() / "Favorites.txt") {
      rescan();
   }

   void draw(visage::Canvas &c) override {
      const float s = height() / 26.0f, w = width(), h = height();
      c.setColor(visage::Color(0x80141a26));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      c.setColor(visage::Color(0x30ffffff));
      c.roundedRectangleBorder(0, 0, w, h, 6 * s, 1.0f * s);
      const float a = kArrow * s, save = kSave * s;
      c.setColor(visage::Color(0x22ffffff));
      c.fill(a, 4 * s, 1 * s, h - 8 * s);
      c.fill(w - save - a, 4 * s, 1 * s, h - 8 * s);
      c.fill(w - save, 4 * s, 1 * s, h - 8 * s);
      const auto arrow = [&](float cx, float dir, bool hot) {
         c.setColor(hot ? kText : kDim);
         c.triangle(cx + dir * 4 * s, h / 2, cx - dir * 3 * s, h / 2 - 5 * s, cx - dir * 3 * s, h / 2 + 5 * s);
      };
      arrow(a / 2, -1.0f, hover_ == Zone::Prev);
      arrow(w - save - a / 2, 1.0f, hover_ == Zone::Next);
      c.setColor(hover_ == Zone::Save ? kText : kPartColors[0]);
      c.text("SAVE", ui::font(11 * s), visage::Font::kCenter, w - save, 0, save, h);
      if (editing())
         return;
      const std::string name = host_.presetName();
      const float star = kStar * s;
      c.setColor(hover_ == Zone::Name ? 0xffffffff : kText);
      c.text(name.empty() ? std::string("Untitled") : name, ui::font(13 * s), visage::Font::kCenter, a, 0,
             w - 2 * a - save - star, h);
      if (indexOf(name) >= 0) {
         const bool starred = favorites_.contains(name);
         c.setColor(starred ? kPartColors[0] : hover_ == Zone::Star ? kText : kDim);
         ui::drawStar(c, w - save - a - star / 2, h / 2, 6.0f * s, starred);
      }
   }

   void mouseMove(const visage::MouseEvent &e) override { setHover(zoneAt(e.position.x)); }
   void mouseExit(const visage::MouseEvent &) override { setHover(Zone::None); }

   void mouseDown(const visage::MouseEvent &e) override {
      switch (zoneAt(e.position.x)) {
      case Zone::Prev: step(-1); break;
      case Zone::Next: step(1); break;
      case Zone::Save: editing() ? finishSave(true) : startSave(); break;
      case Zone::Name: showMenu(); break;
      case Zone::Star: toggleFavorite(); break;
      case Zone::None: break;
      }
   }

   bool mouseWheel(const visage::MouseEvent &e) override {
      // Only a vertical scroll steps, so a sideways trackpad swipe doesn't.
      if (e.wheel_delta_y == 0.0f || std::fabs(e.wheel_delta_x) > std::fabs(e.wheel_delta_y))
         return false;
      step(e.wheel_delta_y > 0 ? -1 : 1);
      return true;
   }

private:
   enum class Zone { None, Prev, Name, Star, Next, Save };
   static constexpr float kArrow = 24.0f, kSave = 48.0f, kStar = 22.0f;
   static constexpr int kSaveId = 1 << 20, kDeleteId = kSaveId + 1, kFavoriteId = kSaveId + 2, kUserBase = 1 << 16,
                        kFavoritesBase = 1 << 18;

   Zone zoneAt(float x) const {
      const float s = height() / 26.0f, w = width();
      if (x < kArrow * s)
         return Zone::Prev;
      if (x >= w - kSave * s)
         return Zone::Save;
      if (x >= w - (kSave + kArrow) * s)
         return Zone::Next;
      if (x >= w - (kSave + kArrow + kStar) * s)
         return Zone::Star;
      return Zone::Name;
   }
   void setHover(Zone z) {
      if (z != hover_) {
         hover_ = z;
         redraw();
      }
   }

   // Reads the user's preset folder: when the panel opens, the menu opens and after a save, not on
   // every arrow click, so stepping through presets never waits on the disk.
   void rescan() {
      favorites_.reload();
      const std::string current = currentIndex_ >= 0 && currentIndex_ < total() ? at(currentIndex_).name : "";
      const bool wasUser = currentIndex_ >= static_cast<int>(factoryPresets().size());
      user_ = userPresets();
      currentIndex_ = -1;
      if (!current.empty() && wasUser)
         for (int i = 0; i < static_cast<int>(user_.size()); ++i)
            if (user_[i].name == current)
               currentIndex_ = static_cast<int>(factoryPresets().size()) + i;
   }

   // Every preset in menu order: factory first, then the user's.
   int total() const { return static_cast<int>(factoryPresets().size() + user_.size()); }
   const Preset &at(int i) const {
      const auto &f = factoryPresets();
      return i < static_cast<int>(f.size()) ? f[i] : user_[i - f.size()];
   }
   // The preset loaded by name: the one last loaded here if it still has that name, otherwise the
   // first in menu order.
   int indexOf(const std::string &name) const {
      if (currentIndex_ >= 0 && currentIndex_ < total() && at(currentIndex_).name == name)
         return currentIndex_;
      for (int i = 0; i < total(); ++i)
         if (at(i).name == name)
            return i;
      return -1;
   }

   void load(int i) {
      const Preset &p = at(i);
      host_.setParams(presetValues(p).data());
      host_.setPresetName(p.name);
      currentIndex_ = i;
      redraw();
   }

   // Stars the current preset, or unstars it; a name that isn't a preset can't be starred.
   void toggleFavorite() {
      const std::string name = host_.presetName();
      if (indexOf(name) < 0)
         return;
      favorites_.toggle(name);
      redraw();
   }

   void step(int dir) {
      const int n = total();
      const int current = indexOf(host_.presetName());
      load(current < 0 ? 0 : (current + dir + n) % n);
   }

   void showMenu() {
      rescan();
      visage::PopupMenu menu;
      const std::string current = host_.presetName();
      // Favorites first, in menu order, for the presets that still exist.
      visage::PopupMenu favorites("Favorites");
      for (int i = 0; i < total(); ++i)
         if (favorites_.contains(at(i).name))
            favorites.addOption(kFavoritesBase + i, at(i).name).select(at(i).name == current);
      if (favorites.options().empty())
         favorites.addOption(-2, "(click the star to add one)").enable(false);
      menu.addSubMenu(std::move(favorites));
      menu.addBreak();
      std::vector<std::string> categories;
      for (const Preset &p : factoryPresets())
         if (std::find(categories.begin(), categories.end(), p.category) == categories.end())
            categories.push_back(p.category);
      const auto &f = factoryPresets();
      for (const std::string &cat : categories) {
         visage::PopupMenu sub(cat);
         for (int i = 0; i < static_cast<int>(f.size()); ++i)
            if (f[i].category == cat)
               sub.addOption(i, f[i].name).select(f[i].name == current);
         menu.addSubMenu(std::move(sub));
      }
      visage::PopupMenu user("User");
      for (int i = 0; i < static_cast<int>(user_.size()); ++i)
         user.addOption(kUserBase + i, user_[i].name).select(user_[i].name == current);
      if (user_.empty())
         user.addOption(-2, "(none saved yet)").enable(false);
      menu.addSubMenu(std::move(user));
      menu.addBreak();
      menu.addOption(kSaveId, "Save as...");
      if (indexOf(current) >= 0)
         menu.addOption(kFavoriteId, favorites_.contains(current) ? "Remove from Favorites" : "Add to Favorites");
      if (onUserPreset())
         menu.addOption(kDeleteId, "Delete \"" + current + "\"...");
      menu.onSelection() += [this](int id) {
         if (id == kSaveId)
            startSave();
         else if (id == kDeleteId)
            confirmDelete();
         else if (id == kFavoriteId)
            toggleFavorite();
         else if (id >= kFavoritesBase && id - kFavoritesBase < total())
            load(id - kFavoritesBase);
         else if (id >= kUserBase && id - kUserBase < static_cast<int>(user_.size()))
            load(static_cast<int>(factoryPresets().size()) + id - kUserBase);
         else if (id >= 0 && id < static_cast<int>(factoryPresets().size()))
            load(id);
      };
      menu.show(this, {kArrow * height() / 26.0f, height()});
   }

   bool onUserPreset() const {
      const int i = indexOf(host_.presetName());
      return i >= static_cast<int>(factoryPresets().size());
   }

   // Deleting a preset can't be undone, so the menu asks once more before the file goes.
   void confirmDelete() {
      const std::string name = host_.presetName();
      visage::PopupMenu menu;
      menu.addOption(kDeleteId, "Delete \"" + name + "\" for good");
      menu.addOption(-2, "Cancel");
      menu.onSelection() += [this, name](int id) {
         if (id != kDeleteId)
            return;
         std::error_code ec;
         std::filesystem::remove(userPresetFolder() / (name + kPresetExtension), ec);
         favorites_.remove(name);
         rescan();
         redraw();
      };
      menu.show(this, {kArrow * height() / 26.0f, height()});
   }

   // The save name box. A click anywhere but SAVE drops the name, so the box never sits open
   // without the keyboard; SAVE itself saves, and so does Enter.
   class NameEditor : public visage::TextEditor {
   public:
      std::function<void()> onClickAway;
      void focusChanged(bool is_focused, bool was_clicked) override {
         visage::TextEditor::focusChanged(is_focused, was_clicked);
         if (!is_focused && was_clicked && onClickAway)
            onClickAway();
      }
   };

   bool editing() const { return editor_ && editor_->isVisible(); }

   void startSave() {
      if (!editor_) {
         editor_ = std::make_unique<NameEditor>();
         editor_->setJustification(visage::Font::kCenter);
         editor_->onEnterKey() += [this] { finishSave(true); };
         editor_->onEscapeKey() += [this] { finishSave(false); };
         editor_->onClickAway = [this] {
            if (hover_ != Zone::Save)
               finishSave(false);
         };
         addChild(editor_.get(), false);
      }
      const float s = height() / 26.0f;
      editor_->setBounds(kArrow * s, 3 * s, width() - (2 * kArrow + kSave) * s, height() - 6 * s);
      editor_->setFont(ui::font(13 * s));
      editor_->setText(host_.presetName().empty() ? std::string("My Flock") : host_.presetName());
      editor_->setVisible(true);
      editor_->requestKeyboardFocus();
      editor_->selectAll();
      redraw();
   }

   void finishSave(bool apply) {
      if (!editing())
         return;
      const std::string name = uniqueName(presetFileName(editor_->text().toUtf8()));
      editor_->setVisible(false);
      if (apply && writePreset(userPresetFolder() / (name + kPresetExtension), host_)) {
         host_.setPresetName(name);
         rescan();
         for (int i = 0; i < static_cast<int>(user_.size()); ++i)
            if (user_[i].name == name)
               currentIndex_ = static_cast<int>(factoryPresets().size()) + i;
      }
      redraw();
   }

   // A save name that won't hide a factory preset or silently replace another user preset: saving
   // again over the user preset you're on keeps its name, anything else that's taken gets a number.
   std::string uniqueName(const std::string &wanted) {
      rescan();
      const bool onUserPreset = currentIndex_ >= static_cast<int>(factoryPresets().size()) &&
                                at(currentIndex_).name == wanted;
      if (onUserPreset)
         return wanted;
      std::string name = wanted;
      for (int k = 2; indexOf(name) >= 0; ++k)
         name = wanted + " " + std::to_string(k);
      return name;
   }

   EditorHost &host_;
   ui::PresetFavorites favorites_;
   std::vector<Preset> user_;
   int currentIndex_ = -1;
   std::unique_ptr<NameEditor> editor_;
   Zone hover_ = Zone::None;
};

// ---- the sky ----

// One species' flock params, read from the host each frame.
struct Species {
   int behaviour = 0;
   float cohesion = 0, alignment = 0, separation = 0, speed = 0, wander = 0, lure = 0, range = 0;
   float neighbourFm = 0, tailLength = 0, tailSpread = 0;
   float wingHz = 0, wingPush = 0, windHz = 0, windPush = 0;

   void read(const EditorHost &host, int part) {
      const auto v = [&](clap_id offset) { return static_cast<float>(host.paramValue(partId(part, offset))); };
      behaviour = static_cast<int>(v(flockParam(kBehaviour)) + 0.5f);
      cohesion = v(flockParam(kCohesion));
      alignment = v(flockParam(kAlignment));
      separation = v(flockParam(kSeparation));
      speed = v(flockParam(kSpeed));
      wander = v(flockParam(kWander));
      lure = v(flockParam(kLure));
      range = v(flockParam(kRange));
      neighbourFm = v(flockParam(kNeighbourFm));
      tailLength = v(flockParam(kTailLength));
      tailSpread = v(flockParam(kTailSpread));
      wingHz = static_cast<float>(lfoHz(v(lfoParam(0, kLfoRate))));
      wingPush = v(lfoParam(0, kLfoPush));
      windHz = static_cast<float>(lfoHz(v(lfoParam(1, kLfoRate))));
      windPush = v(lfoParam(1, kLfoPush));
   }
};

// The stage: a night sky, both flocks, their ink, the links between neighbours that modulate each
// other, and the lures of the held notes. Click and drag in the sky to play and steer a creature.
class FlockView : public visage::Frame {
public:
   explicit FlockView(EditorHost &host, float designWidth) : host_(host), designWidth_(designWidth), rng_(23) {
      bloom_.setBloomSize(18.0f);
      bloom_.setBloomIntensity(1.0f);
      setPostEffect(&bloom_);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      for (Star &st : stars_)
         st = {u(rng_), u(rng_), u(rng_), u(rng_) * 6.28f};
      local_.reset(23);
      puffs_.reserve(kMaxPuffs);
   }

   // The part whose creatures are drawn in front, brighter, and whose sources the chips show.
   int shownPart = 0;
   // Where the sky is, in design units from the view's top left: its left, top and height (its width
   // follows from kSkyAspect). The panel moves it when its panels are hidden.
   float skyLeft = kSkyX, skyTop = kSkyY - kHeaderHeight, skyHeight = kSkyH;
   // Whether each held note's lure ring is drawn.
   bool showRings = true;
   // A line of help at the top of the sky (MAP mode's), in its colour.
   std::string hint;
   unsigned hintColor = kText;

   // One frame, `dt` seconds after the last: the engine's flock as it last published it, or while
   // the plugin isn't processing, the panel's own.
   void advance(const std::vector<EditorHost::Note> &notes, float dt) {
      time_ += dt;
      for (int part = 0; part < kPartCount; ++part)
         species_[part].read(host_, part);
      live_ = host_.flockFrame(frame_);
      if (!live_)
         flyLocal(notes, dt);
      sync();
      shed(dt);
      moveInk(dt);
      redraw();
   }

   // Whether the creatures are the engine's (the plugin is processing).
   bool live() const { return live_; }
   // The engine's newest envelope and LFO levels for `part`, while live.
   float envLevel(int part, int e) const { return frame_.env[part][e]; }
   float lfoLevel(int part, int l) const { return frame_.lfo[part][l]; }
   // The engine's newest frame, while live.
   const FlockFrame &frame() const { return frame_; }
   // `part`'s param at `offset` as its newest voice has it now, while it sounds and a route or the
   // host's per-note modulation moves it; otherwise its knob's value.
   double value(int part, clap_id offset) const {
      if (live_) {
         const int d = destinationIndex(offset);
         if (d >= 0 && frame_.moved[part][d])
            return frame_.dest[part][d];
      }
      return host_.paramValue(partId(part, offset));
   }

   // The sources of `part`'s newest held creature, or a fading one's when it holds none.
   CreatureSources sources(int part) const {
      const Creature *pick = nullptr;
      for (const Creature &b : creatures_)
         if (b.part == part && (b.held ? !pick || !pick->held || b.id > pick->id : !pick))
            pick = &b;
      return pick ? pick->sources : CreatureSources{};
   }

   void draw(visage::Canvas &c) override {
      const float s = width() / designWidth_;
      bloom_.setBloomSize(18.0f * s * (dpiScale() > 0.0f ? dpiScale() : 1.0f));
      drawBackdrop(c, s);
      const float ppu = skyHeight * s;
      const float ox = skyLeft * s, oy = skyTop * s;
      const auto X = [&](float x) { return ox + x * ppu; };
      const auto Y = [&](float y) { return oy + y * ppu; };

      // Ink, oldest first: soft discs that bloom wider and thinner as they age.
      for (const Puff &p : puffs_) {
         const float t = p.age / p.life;
         const float a = p.alpha * std::min(1.0f, p.age * 10.0f) * std::pow(1.0f - t, 1.6f) *
                         (p.part == shownPart ? 0.22f : 0.12f) * (1.0f + 0.4f * p.heat) /
                         std::sqrt(std::max(1.0f, p.crowd / 6.0f));
         if (a < 0.003f)
            continue;
         const float r = p.size * ppu;
         // Hot ink, pressed against another creature's, goes whiter and blooms.
         visage::Color tint = visage::Color(p.color).interpolateWith(0xffffffff, std::min(0.35f, 0.2f * p.heat));
         tint.setHdr(1.0f + 0.25f * (1.0f - t) + 0.5f * p.heat);
         c.setColor(visage::Brush::radial(tint.withAlpha(a), tint.withAlpha(0.0f), visage::Point(X(p.x), Y(p.y)), r));
         c.circle(X(p.x) - r, Y(p.y) - r, 2 * r);
      }

      // Links: a creature's nearest of its kind, brighter the more Neighbour FM it hears. A hunter
      // that has caught its prey bites it, in pink.
      const auto bySlot = [&](int slot) -> const Creature * {
         for (const Creature &o : creatures_)
            if (o.slot == slot)
               return &o;
         return nullptr;
      };
      for (const Creature &a : creatures_) {
         if (const Creature *b = a.neighbour >= 0 ? bySlot(a.neighbour) : nullptr) {
            const float e = std::min(a.energy, b->energy);
            const float k = e * (0.1f + 0.7f * species_[a.part].neighbourFm);
            if (k > 0.02f) {
               c.setColor(glow(kText, k * (a.part == shownPart ? 0.7f : 0.35f), 1.2f));
               c.segment(X(a.x), Y(a.y), X(b->x), Y(b->y), 1.0f * s, true);
            }
         }
         if (const Creature *h = a.hunter >= 0 && a.sources.caught > 0.3f ? bySlot(a.hunter) : nullptr) {
            c.setColor(glow(kCaughtColor, 0.9f * std::min(a.energy, h->energy), 1.8f));
            c.segment(X(a.x), Y(a.y), X(h->x), Y(h->y), 2.2f * s, true);
         }
      }

      // Lures: where each held note pulls its creature. Faint, so they read as guides, not as
      // things in the sky; RINGS turns them off.
      for (const Creature &b : creatures_) {
         if (!showRings || !b.held || b.energy < 0.05f)
            continue;
         const unsigned col = kPartColors[b.part];
         c.setColor(glow(col, 0.06f * b.energy, 1.0f));
         c.segment(X(b.lureX), Y(b.lureY), X(b.x), Y(b.y), 0.8f * s, true);
         c.setColor(glow(col, 0.28f * b.energy, 1.0f));
         const float r = (9.0f + 5.0f * b.press) * s;
         c.ring(X(b.lureX) - r, Y(b.lureY) - r, 2 * r, 0.9f * s);
      }

      // The other species first, so the shown one flies in front.
      for (int pass = 0; pass < 2; ++pass)
         for (const Creature &b : creatures_)
            if ((b.part == shownPart) == (pass == 1))
               drawCreature(c, b, s, X, Y);

      if (!hint.empty()) {
         c.setColor(glow(hintColor, 0.95f, 1.0f));
         c.text(hint, ui::font(12 * s), visage::Font::kCenter, skyLeft * s, (skyTop - 2) * s, skyHeight * kSkyAspect * s, 18 * s);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (e.isRightButton()) {
         if (onBackgroundMenu)
            onBackgroundMenu(e.position);
         return;
      }
      float nx, timbre;
      if (!skyAt(e.position, nx, timbre))
         return;
      playing_ = std::clamp(static_cast<int>(std::lround(36.0f + 48.0f * nx)), 0, 127);
      host_.noteOn(playing_, 0.7f);
      host_.noteExpression(playing_, 0.0f, 0.6f, timbre);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (playing_ < 0)
         return;
      float nx, timbre;
      skyAt(e.position, nx, timbre);
      host_.noteExpression(playing_, std::clamp(36.0f + 48.0f * nx - playing_, -48.0f, 48.0f), 0.6f, timbre);
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (playing_ >= 0)
         host_.noteOff(playing_);
      playing_ = -1;
   }

   // Right-click on the stage, with where (in the view).
   std::function<void(visage::Point)> onBackgroundMenu;

private:
   static constexpr int kMaxPuffs = 2600;
   static constexpr int kPathPoints = 22;

   // A creature as drawn: the flock's, plus its colour, fresh ink stroke and shedding clock.
   struct Creature {
      uint32_t id = 0;
      int part = 0, slot = 0, neighbour = -1, hunter = -1;
      bool held = false;
      unsigned color = 0;
      float x = 0, y = 0, vx = 0, vy = 0;
      float energy = 0;
      float lureX = 0, lureY = 0, press = 0;
      float wing = 0, emit = 0;
      CreatureSources sources;
      std::vector<visage::Point> path;
   };
   struct Puff {
      float x, y, vx, vy, age, life, size, grow, alpha;
      unsigned color;
      int part;
      // The id of the creature that shed it: a creature stirs only others' ink, and only other
      // creatures' ink pushes back.
      uint32_t owner;
      // How hard it is pressed against other ink (it glows whiter), and how many puffs overlap it
      // (each draws thinner, so a dense knot stays a cloud).
      float heat = 0.0f, crowd = 0.0f;
   };
   struct Star {
      float x, y, b, phase;
   };

   bool skyAt(visage::Point p, float &nx, float &timbre) const {
      const float s = width() / designWidth_;
      const float x = (p.x / s - skyLeft) / skyHeight, y = (p.y / s - skyTop) / skyHeight;
      nx = std::clamp((x / kSkyAspect - 0.07f) / 0.86f, 0.0f, 1.0f);
      timbre = std::clamp(1.0f - (y - 0.1f) / 0.8f, 0.0f, 1.0f);
      return x >= 0.0f && x <= kSkyAspect && y >= 0.0f && y <= 1.0f;
   }

   // Whether a note hatches a creature in `part`, by the play mode.
   bool plays(int part, const EditorHost::Note &n) const {
      switch (static_cast<int>(host_.paramValue(kPlayMode) + 0.5)) {
      case 0: // Single
         return part == 0;
      case 2: // Split: B below the split key, A from it up
         return (n.note < static_cast<int>(host_.paramValue(kSplitKey) + 0.5)) == (part == 1);
      case 4: // Velocity: B for hard hits
         return (n.velocity * 127.0f >= static_cast<float>(host_.paramValue(kVelocitySplit))) == (part == 1);
      default: // Layer, zones and the crossfade play both
         return true;
      }
   }

   // The frame's creatures, keeping each one's colour and ink stroke while its id lasts.
   void sync() {
      std::vector<Creature> next;
      next.reserve(static_cast<size_t>(frame_.count));
      for (int i = 0; i < frame_.count; ++i) {
         const FlockFrame::Shown &s = frame_.creatures[i];
         Creature b;
         for (Creature &old : creatures_)
            if (old.id == s.id) {
               b = std::move(old);
               break;
            }
         if (b.id != s.id) {
            b = Creature{};
            b.id = s.id;
            b.color = kInks[s.part][(s.slot * 5 + static_cast<int>(s.id)) & 3];
            b.path.reserve(kPathPoints);
         }
         const bool jumped = std::fabs(s.x - b.x) > 0.3f || std::fabs(s.y - b.y) > 0.3f;
         b.part = s.part, b.slot = s.slot, b.neighbour = s.neighbour, b.hunter = s.hunter;
         b.held = s.held;
         b.x = s.x, b.y = s.y, b.vx = s.vx, b.vy = s.vy;
         b.energy = s.energy, b.press = s.press, b.wing = s.wing;
         b.lureX = s.lureX, b.lureY = s.lureY;
         b.sources = s.sources;
         if (jumped) // wrapped round the sky
            b.path.clear();
         if (static_cast<int>(b.path.size()) == kPathPoints)
            b.path.erase(b.path.begin());
         b.path.push_back({b.x, b.y});
         next.push_back(std::move(b));
      }
      creatures_ = std::move(next);
   }

   // The panel's own flock, while the plugin isn't processing: the same sim, fed the held notes and
   // the params without their routes.
   void flyLocal(const std::vector<EditorHost::Note> &notes, float dt) {
      local_.size = static_cast<float>(host_.paramValue(kSkySize));
      local_.walls = static_cast<int>(host_.paramValue(kSkyWalls) + 0.5);
      for (int part = 0; part < kPartCount; ++part)
         local_.gust[part] = std::sin(2.0f * kPi * species_[part].windHz * time_ + 1.0f);
      bool claimed[kVoiceSlots] = {};
      for (const EditorHost::Note &n : notes)
         for (int part = 0; part < kPartCount; ++part) {
            if (!plays(part, n))
               continue;
            int slot = -1;
            for (int i = 0; i < kVoiceSlots && slot < 0; ++i)
               if (!claimed[i] && localNote_[i] == n.note && local_.creatures[i].alive && local_.creatures[i].held &&
                   local_.creatures[i].part == part)
                  slot = i;
            float lx, ly;
            const float touch[kTouchCount] = {n.velocity, n.pressure, 0.0f, n.timbre, n.release};
            // A random place of its own, the same each time the note is played.
            const float random = std::sin(n.note * 12.9898f) * 2.0f - 1.0f;
            const auto axis = [&](FlockParam p) {
               return lureAxis(static_cast<int>(host_.paramValue(partId(part, flockParam(p))) + 0.5), static_cast<float>(n.note),
                               n.bend, touch, random);
            };
            lureAt(axis(kLureX), axis(kLureY), lx, ly);
            if (slot < 0) {
               for (int i = 0; i < kVoiceSlots && slot < 0; ++i)
                  if (!claimed[i] && !local_.creatures[i].alive)
                     slot = i;
               for (int i = 0; i < kVoiceSlots && slot < 0; ++i)
                  if (!claimed[i] && !local_.creatures[i].held)
                     slot = i;
               if (slot < 0)
                  continue;
               local_.hatch(slot, part, lx, ly);
               localNote_[slot] = n.note;
            }
            claimed[slot] = true;
            jsynth::Creature &c = local_.creatures[slot];
            c.lureX = lx;
            c.lureY = ly;
            c.press = std::clamp(n.pressure, 0.0f, 1.0f);
         }
      for (int i = 0; i < kCreatureSlots; ++i) {
         jsynth::Creature &c = local_.creatures[i];
         if (!c.alive)
            continue;
         if (!claimed[i])
            local_.release(i);
         c.params.read([&](clap_id o) { return host_.paramValue(partId(c.part, o)); });
         localWing_[i] += dt * species_[c.part].wingHz;
         c.wing = std::sin(2.0f * kPi * localWing_[i]);
      }
      localTime_ += std::min(dt, 0.1f);
      while (localTime_ >= Flock::kStep) {
         localTime_ -= Flock::kStep;
         local_.step();
      }
      local_.show(frame_);
   }

   // Every creature sheds ink behind it, more the brighter it is.
   void shed(float dt) {
      std::uniform_real_distribution<float> u(-1.0f, 1.0f);
      for (Creature &b : creatures_) {
         if (b.energy < 0.03f)
            continue;
         const Species &sp = species_[b.part];
         b.emit -= dt;
         while (b.emit <= 0.0f && static_cast<int>(puffs_.size()) < kMaxPuffs) {
            b.emit += 1.0f / 50.0f;
            Puff p;
            p.x = b.x - b.vx * 0.03f + 0.004f * u(rng_);
            p.y = b.y - b.vy * 0.03f + 0.004f * u(rng_);
            p.vx = -0.12f * b.vx + 0.015f * u(rng_);
            p.vy = -0.12f * b.vy + 0.015f * u(rng_);
            p.age = 0.0f;
            p.life = (0.6f + 4.4f * sp.tailLength) * (0.75f + 0.25f * u(rng_));
            p.size = 0.007f + 0.006f * b.press;
            p.grow = 0.008f + 0.07f * sp.tailSpread;
            p.alpha = b.energy;
            p.color = b.color;
            p.part = b.part;
            p.owner = b.id;
            puffs_.push_back(p);
         }
         b.emit = std::max(b.emit, 0.0f);
      }
   }

   // Ink drifts on a slow curl and slows. Like JVerb's gas, one creature's ink pushes back where it
   // overlaps another's, harder against the other species, and the seam heats up and glows. Any
   // creature flying through ink stirs it along.
   void moveInk(float dt) {
      // Sort the puffs into a coarse grid over the sky (and a margin), so each only checks its
      // neighbours; a crowded spot checks an even sample of at most kMaxChecks, weighted up to match.
      constexpr float kCell = 0.06f;
      constexpr int kGridX = 30, kGridY = 22, kMaxChecks = 32;
      for (auto &cell : grid_)
         cell.clear();
      const auto cellX = [&](float x) { return std::clamp(static_cast<int>(std::floor(x / kCell)) + 3, 0, kGridX - 1); };
      const auto cellY = [&](float y) { return std::clamp(static_cast<int>(std::floor(y / kCell)) + 3, 0, kGridY - 1); };
      grid_.resize(kGridX * kGridY);
      for (int i = 0; i < static_cast<int>(puffs_.size()); ++i)
         grid_[cellY(puffs_[i].y) * kGridX + cellX(puffs_[i].x)].push_back(i);

      const float drag = std::exp(-dt * 1.4f);
      for (int i = 0; i < static_cast<int>(puffs_.size()); ++i) {
         Puff &p = puffs_[i];
         p.age += dt;
         p.size += p.grow * dt;
         float ax = std::sin(p.y * 7.0f + time_ * 0.21f) * 0.012f, ay = std::cos(p.x * 6.0f - time_ * 0.17f) * 0.012f;
         float contact = 0.0f, crowd = 0.0f;
         const int cx = cellX(p.x), cy = cellY(p.y);
         const int gx0 = std::max(0, cx - 1), gx1 = std::min(kGridX - 1, cx + 1);
         const int gy0 = std::max(0, cy - 1), gy1 = std::min(kGridY - 1, cy + 1);
         int candidates = 0;
         for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx)
               candidates += static_cast<int>(grid_[gy * kGridX + gx].size());
         const int stride = (candidates + kMaxChecks - 1) / kMaxChecks;
         const float weight = static_cast<float>(std::max(1, stride));
         int seen = stride > 1 ? i % stride : 0;
         for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx)
               for (int j : grid_[gy * kGridX + gx]) {
                  if (stride > 1 && seen++ % stride != 0)
                     continue;
                  const Puff &q = puffs_[j];
                  const float dx = p.x - q.x, dy = p.y - q.y, reach = 0.6f * (p.size + q.size);
                  const float d2 = dx * dx + dy * dy;
                  if (d2 >= reach * reach)
                     continue;
                  crowd += weight;
                  if (q.owner == p.owner)
                     continue;
                  const float d = std::sqrt(d2) + 1e-4f;
                  const float overlap = weight * (1.0f - d / reach) * (q.part == p.part ? 1.0f : 1.8f);
                  ax += 0.025f * overlap * dx / d;
                  ay += 0.025f * overlap * dy / d;
                  contact += overlap;
               }
         p.heat += (std::min(2.0f, 0.5f * contact) - p.heat) * std::min(1.0f, dt * 3.0f);
         p.crowd = crowd;
         p.vx = p.vx * drag + ax * dt;
         p.vy = p.vy * drag + ay * dt;
         for (const Creature &b : creatures_) {
            if (b.id == p.owner || b.energy < 0.05f)
               continue;
            const float dx = p.x - b.x, dy = p.y - b.y, d2 = dx * dx + dy * dy;
            if (d2 < 0.0025f) {
               const float k = (1.0f - std::sqrt(d2) / 0.05f) * 0.6f * dt;
               p.vx += b.vx * k;
               p.vy += b.vy * k;
            }
         }
         p.x += p.vx * dt;
         p.y += p.vy * dt;
      }
      puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff &p) { return p.age >= p.life; }),
                   puffs_.end());
   }

   template <typename FX, typename FY>
   void drawCreature(visage::Canvas &c, const Creature &b, float s, FX X, FY Y) {
      if (b.energy < 0.01f)
         return;
      const float shown = b.part == shownPart ? 1.0f : 0.6f;
      // Fresh ink: a bright stroke that thins out behind it, before it blooms into the cloud.
      const size_t n = b.path.size();
      for (size_t i = 1; i < n; ++i) {
         const float k = static_cast<float>(i) / static_cast<float>(n);
         c.setColor(glow(b.color, k * k * 0.8f * b.energy * shown, 1.0f + 0.4f * k));
         c.segment(X(b.path[i - 1].x), Y(b.path[i - 1].y), X(b.path[i].x), Y(b.path[i].y), (0.6f + 2.6f * k) * s, true);
      }
      const float x = X(b.x), y = Y(b.y);
      const float r = (9.0f + 5.0f * b.press) * s * (0.6f + 0.4f * b.energy);
      visage::Color core = visage::Color(b.color).interpolateWith(0xffffffff, 0.55f);
      core.setHdr(1.2f + 0.8f * b.energy * shown);
      c.setColor(visage::Brush::radial(core.withAlpha(b.energy * shown), glow(b.color, 0.0f, 1.0f), visage::Point(x, y), r));
      c.circle(x - r, y - r, 2 * r);
      // Wings: a chevron along its heading that beats with the wingbeat.
      const float v = std::hypot(b.vx, b.vy) + 1e-5f, hx = b.vx / v, hy = b.vy / v;
      const float flap = 0.45f + 0.35f * b.wing;
      const float len = (7.0f + 3.0f * b.press) * s;
      c.setColor(glow(0xffffffff, 0.85f * b.energy * shown, 1.3f));
      for (int side = -1; side <= 1; side += 2) {
         const float wx = -hx * std::cos(flap) - side * hy * std::sin(flap) * 1.4f;
         const float wy = -hy * std::cos(flap) + side * hx * std::sin(flap) * 1.4f;
         c.segment(x + hx * 2 * s, y + hy * 2 * s, x + wx * len, y + wy * len, 1.4f * s, true);
      }
      if (b.sources.caught > 0.05f) {
         const float rr = r * (1.6f + 0.6f * (1.0f - b.sources.caught));
         c.setColor(glow(kCaughtColor, b.sources.caught * 0.6f, 1.4f));
         c.ring(x - rr, y - rr, 2 * rr, 1.5f * s);
      }
   }

   void drawBackdrop(visage::Canvas &c, float s) {
      const float w = width(), h = height();
      c.setColor(0xff04050a);
      c.fill(0, 0, w, h);
      const auto nebula = [&](float x, float y, float r, unsigned color, float a) {
         c.setColor(visage::Brush::radial(glow(color, a, 1.0f), glow(color, 0.0f, 1.0f), visage::Point(x * s, y * s), r * s));
         c.circle((x - r) * s, (y - r) * s, 2 * r * s);
      };
      nebula(380, 240, 420, 0xff5a2a3a, 0.22f);
      nebula(860, 330, 440, 0xff12355e, 0.26f);
      nebula(620, 60, 300, 0xff3a2a6e, 0.14f);
      nebula(200, 620, 360, 0xff1b4550, 0.14f);
      for (const Star &st : stars_) {
         const float twinkle = 0.8f + 0.2f * std::sin(time_ * 1.1f + st.phase);
         const float r = (0.5f + st.b * st.b * 1.5f) * s;
         c.setColor(glow(0xffffffff, (0.12f + 0.55f * st.b) * twinkle, st.b > 0.95f ? 1.4f : 1.0f));
         c.circle(st.x * w - r, st.y * h - r, 2 * r);
      }
      // The sky's corners, faintly, so the walls have somewhere to be.
      const float x0 = skyLeft * s, y0 = skyTop * s, x1 = x0 + skyHeight * kSkyAspect * s, y1 = y0 + skyHeight * s,
                  t = 12 * s;
      c.setColor(visage::Color(0x30ffffff));
      for (float cx : {x0, x1})
         for (float cy : {y0, y1}) {
            const float dx = cx == x0 ? t : -t, dy = cy == y0 ? t : -t;
            c.segment(cx, cy, cx + dx, cy, 1.0f * s, true);
            c.segment(cx, cy, cx, cy + dy, 1.0f * s, true);
         }
   }

   EditorHost &host_;
   float designWidth_;
   std::mt19937 rng_;
   visage::BloomPostEffect bloom_;
   Species species_[kPartCount];
   std::vector<Creature> creatures_;
   // The frame being drawn: the engine's, or the panel's own flock's.
   FlockFrame frame_;
   bool live_ = false;
   Flock local_;
   int localNote_[kVoiceSlots] = {};
   float localWing_[kVoiceSlots] = {};
   float localTime_ = 0.0f;
   std::vector<Puff> puffs_;
   std::vector<std::vector<int>> grid_;
   Star stars_[150];
   float time_ = 0.0f;
   int playing_ = -1;
};

// ---- the panel ----

// The panel's drawings. Each reads the shown part's params as it draws, as the newest voice has them
// while it sounds, so it follows every knob, route and preset without bindings of its own.

// One oscillator's wave, two cycles of it, as its Wave and Shape make it.
class WaveView : public visage::Frame {
public:
   WaveView(EditorHost &host, const FlockView &sky, int osc, unsigned color)
       : host_(host), sky_(sky), osc_(osc), color_(color) {
      setIgnoresMouseEvents(true, false);
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 84.0f;
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      c.setColor(visage::Color(0x18ffffff));
      c.fill(0, h / 2, w, 1);
      const auto param = [&](clap_id offset) { return sky_.value(host_.part, offset); };
      const int wave = static_cast<int>(param(oscParam(osc_, kOscWave)) + 0.5);
      const float shape = static_cast<float>(param(oscParam(osc_, kOscShape)));
      const float level = static_cast<float>(param(mixParam(kMixLevel, osc_)));
      constexpr int kPoints = 160;
      std::minstd_rand rng(7 + osc_);
      std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
      float pink = 0.0f;
      float last = 0.0f;
      for (int i = 0; i <= kPoints; ++i) {
         const float t = static_cast<float>(i) / kPoints;
         float y;
         if (wave == dsp::kRandom) {
            // A different tonal wave each half cycle, as a hint that each note takes its own.
            y = dsp::wave((i * 6 / kPoints * 5 + osc_ * 3) % (dsp::kAltCamel + 1), std::fmod(6.0f * t, 1.0f),
                          6.0f / kPoints * 0.01f, shape);
         }
         else if (wave >= dsp::kWhite) {
            const float n = noise(rng);
            pink = 0.8f * pink + 0.2f * n;
            y = wave == dsp::kWhite ? n * 0.8f : pink * 2.0f;
         }
         else
            y = dsp::wave(wave, std::fmod(2.0f * t, 1.0f), 2.0f / kPoints * 0.01f, shape);
         y = std::clamp(y, -1.0f, 1.0f);
         const float px = 8 * s + t * (w - 16 * s), py = h / 2 - y * (h / 2 - 10 * s);
         if (i > 0) {
            const float lx = 8 * s + (t - 1.0f / kPoints) * (w - 16 * s);
            c.setColor(glow(color_, 0.12f, 1.0f));
            c.segment(lx, last, px, py, 7 * s, true);
            c.setColor(glow(color_, 0.35f + 0.65f * std::min(1.0f, level * 2.0f + 0.2f), 1.2f));
            c.segment(lx, last, px, py, 2 * s, true);
         }
         last = py;
      }
   }

private:
   EditorHost &host_;
   const FlockView &sky_;
   int osc_;
   unsigned color_;
};

// Both filters' responses over 20 Hz to 20 kHz, and what the voice hears through them: while the
// part sounds, as its newest voice has them now, routes and key tracking and all, over a faint line
// of where the knobs set them. Each filter has a handle at its knob's cutoff: drag it across for cutoff and up or down for resonance,
// scroll for its drive, double-click to reset. In MAP with a source armed, dragging across sets how
// much the source moves that cutoff instead.
class FilterView : public visage::Frame {
public:
   FilterView(EditorHost &host, Mapper &mapper, const FlockView &sky) : host_(host), mapper_(mapper), sky_(sky) {}

   void mouseDown(const visage::MouseEvent &e) override {
      drag_ = handleAt(e.position.x, e.position.y);
      if (drag_ < 0)
         return;
      const int part = host_.part;
      startX_ = e.position.x, startY_ = e.position.y;
      if (mapper_.mapping() && mapper_.armedSource() != kSourceOff) {
         route_ = mapper_.routeFor(part, destinationIndex(filterParam(drag_, kFilterCutoff)));
         if (route_ >= 0) {
            mapper_.beginRoute(part, route_);
            startCut_ = mapper_.routeAmount(part, route_);
         }
         return;
      }
      route_ = -1;
      if (e.repeatClickCount() == 2) {
         for (FilterParam q : {kFilterCutoff, kFilterResonance})
            set(q, findParam(id(q))->def.defaultValue);
         drag_ = -1;
         return;
      }
      startCut_ = param(filterParam(drag_, kFilterCutoff));
      startRes_ = param(filterParam(drag_, kFilterResonance));
      for (FilterParam q : {kFilterCutoff, kFilterResonance})
         host_.beginEdit(id(q));
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (drag_ < 0)
         return;
      const float fine = e.isShiftDown() ? 0.2f : 1.0f;
      const double across = (e.position.x - startX_) / width() * fine, up = (startY_ - e.position.y) / height() * fine;
      if (route_ >= 0) {
         mapper_.setRouteAmount(host_.part, route_, std::clamp(startCut_ + 2.0 * across, -1.0, 1.0));
         return;
      }
      if (mapper_.mapping())
         return;
      host_.setParam(id(kFilterCutoff), std::clamp(startCut_ + across, 0.0, 1.0));
      host_.setParam(id(kFilterResonance), std::clamp(startRes_ + up, 0.0, 1.0));
      redraw();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (drag_ >= 0 && route_ >= 0)
         mapper_.endRoute(host_.part, route_);
      else if (drag_ >= 0 && !mapper_.mapping())
         for (FilterParam q : {kFilterCutoff, kFilterResonance})
            host_.endEdit(id(q));
      drag_ = -1, route_ = -1;
   }
   bool mouseWheel(const visage::MouseEvent &e) override {
      const int f = handleAt(e.position.x, e.position.y);
      if (f < 0)
         return false;
      drag_ = f;
      set(kFilterDrive, std::clamp(param(filterParam(f, kFilterDrive)) + 0.05 * e.precise_wheel_delta_y, 0.0, 1.0));
      drag_ = -1;
      return true;
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 120.0f;
      const unsigned color = kPartColors[host_.part];
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      // Grid: decades across, 12 dB steps down.
      for (double f : {100.0, 1000.0, 10000.0}) {
         const float x = xOf(f, w);
         c.setColor(visage::Color(0x18ffffff));
         c.fill(x, 0, 1, h);
         c.setColor(visage::Color(0x55ffffff));
         c.text(f >= 1000.0 ? std::to_string(static_cast<int>(f / 1000)) + "k" : std::to_string(static_cast<int>(f)),
                ui::font(9 * s), visage::Font::kLeft, x + 3 * s, h - 14 * s, 30 * s, 12 * s);
      }
      for (int db = 12; db >= -36; db -= 12) {
         c.setColor(visage::Color(db == 0 ? 0x30ffffff : 0x14ffffff));
         c.fill(0, yOf(db, h), w, 1);
      }
      const bool parallel = param(tailParam(kFilterRouting)) > 0.5;
      const double w2 = 0.5 + 0.5 * param(tailParam(kFilterBalance));
      // The newest voice's filters now, if the part is sounding.
      const FlockFrame &frame = sky_.frame();
      live_ = sky_.live() && frame.cutoff[host_.part][0] > 0.0f;
      for (int f = 0; live_ && f < kFilterCount; ++f) {
         liveHz_[f] = frame.cutoff[host_.part][f];
         liveRes_[f] = frame.resonance[host_.part][f];
      }
      constexpr int kPoints = 200;
      float sum[kPoints + 1], knobSet[kPoints + 1], one[kFilterCount][kPoints + 1];
      const auto mix = [&](double a, double b) {
         return static_cast<float>(parallel ? std::min(1.0, 2.0 - 2.0 * w2) * a + std::min(1.0, 2.0 * w2) * b : a * b);
      };
      for (int i = 0; i <= kPoints; ++i) {
         const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / kPoints);
         const double a = magnitude(0, f, live_), b = magnitude(1, f, live_);
         one[0][i] = static_cast<float>(a);
         one[1][i] = static_cast<float>(b);
         sum[i] = mix(a, b);
         knobSet[i] = live_ ? mix(magnitude(0, f, false), magnitude(1, f, false)) : sum[i];
      }
      // What the voice hears, filled; each filter on its own as a line; and while it sounds, where
      // the knobs set it, faint.
      const auto y = [&](float m) { return yOf(20.0 * std::log10(std::max(m, 1e-4f)), h); };
      for (int i = 0; live_ && i < kPoints; ++i) {
         c.setColor(visage::Color(0x55ffffff));
         c.segment(w * i / kPoints, y(knobSet[i]), w * (i + 1) / kPoints, y(knobSet[i + 1]), 1.0f * s, true);
      }
      for (int i = 0; i < kPoints; ++i) {
         const float x0 = w * i / kPoints, x1 = w * (i + 1) / kPoints, y0 = y(sum[i]), y1 = y(sum[i + 1]);
         c.setColor(glow(color, 0.18f, 1.0f));
         c.fill(std::floor(x0), std::max(y0, y1), std::floor(x1) - std::floor(x0), h - std::max(y0, y1));
         c.setColor(glow(color, 1.0f, 1.4f));
         c.segment(x0, y0, x1, y1, 2.5f * s, true);
      }
      for (int f = 0; f < kFilterCount; ++f)
         for (int i = 0; i < kPoints; ++i) {
            c.setColor(glow(f == 0 ? 0xffffffff : 0xff9fd8ff, 0.35f, 1.0f));
            c.segment(w * i / kPoints, y(one[f][i]), w * (i + 1) / kPoints, y(one[f][i + 1]), 1.2f * s, true);
         }
      // The handles: a glowing dot on each filter's own curve at its cutoff.
      for (int f = 0; f < kFilterCount; ++f) {
         const auto [hx, hy] = handle(f);
         const unsigned hc = f == 0 ? 0xffffffff : 0xff9fd8ff;
         const float r = (drag_ == f ? 9.0f : 7.0f) * s;
         c.setColor(glow(hc, 0.25f, 1.0f));
         c.circle(hx - 2 * r, hy - 2 * r, 4 * r);
         c.setColor(glow(f == 0 ? color : hc, 1.0f, 1.5f));
         c.circle(hx - r, hy - r, 2 * r);
         c.setColor(visage::Color(0xff0b0e17));
         c.text(f == 0 ? "1" : "2", ui::font(9 * s), visage::Font::kCenter, hx - r, hy - r, 2 * r, 2 * r);
      }
      c.setColor(visage::Color(0x88ffffff));
      c.text(parallel ? "PARALLEL" : "SERIAL: FILTER 1 INTO FILTER 2", ui::font(9 * s), visage::Font::kRight, 0, 4 * s,
             w - 8 * s, 12 * s);
   }

private:
   double param(clap_id offset) const { return host_.paramValue(partId(host_.part, offset)); }
   clap_id id(FilterParam q) const { return partId(host_.part, filterParam(drag_, q)); }
   // One whole edit of the dragged filter's param.
   void set(FilterParam q, double v) {
      host_.beginEdit(id(q));
      host_.setParam(id(q), v);
      host_.endEdit(id(q));
      redraw();
   }
   // Where filter `f`'s handle is: at its knob's cutoff, on the curve the knobs set (clamped into the
   // graph).
   std::pair<float, float> handle(int f) const {
      const double hz = cutoffHz(param(filterParam(f, kFilterCutoff)));
      const float m = static_cast<float>(magnitude(f, hz, false));
      const float y = yOf(20.0 * std::log10(std::max(m, 1e-4f)), height());
      return {std::clamp(xOf(hz, width()), 10.0f, width() - 10.0f), std::clamp(y, 10.0f, height() - 10.0f)};
   }
   int handleAt(float x, float y) const {
      int best = -1;
      float near = height() * 0.2f;
      for (int f = 0; f < kFilterCount; ++f) {
         const auto [hx, hy] = handle(f);
         const float d = std::hypot(hx - x, hy - y);
         if (d < near)
            best = f, near = d;
      }
      return best;
   }
   static float xOf(double f, float w) { return static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * w; }
   static float yOf(double db, float h) { return static_cast<float>((24.0 - std::clamp(db, -48.0, 24.0)) / 72.0) * h; }

   // A sketch of filter `f`'s magnitude at `hz`: the analog prototypes, close enough to see. `live`
   // takes the cutoff and resonance the newest voice has now, else the knobs'.
   double magnitude(int f, double hz, bool live) const {
      const int type = static_cast<int>(param(filterParam(f, kFilterType)) + 0.5);
      const int stages = static_cast<int>(param(filterParam(f, kFilterSlope)) + 0.5) + 1;
      const double fc = live ? liveHz_[f] : cutoffHz(param(filterParam(f, kFilterCutoff)));
      const double res = live ? liveRes_[f] : param(filterParam(f, kFilterResonance));
      const double x = hz / fc, q = 0.707 + 9.0 * res * res;
      const auto two = [&](double qq) { return 1.0 / std::sqrt((1 - x * x) * (1 - x * x) + x * x / (qq * qq)); };
      switch (type) {
      case 0: return std::pow(two(0.707), stages - 1) * two(q);
      case 1: return std::pow(x * x * two(0.707), stages - 1) * x * x * two(q);
      case 2: return std::pow(x / 0.707 * two(0.707), stages - 1) * (x / q) * two(q);
      case 3: return std::fabs(1 - x * x) * two(q);
      case 4: return 1.0 + 4.0 * res * (x / q) * two(q);
      case 5: {
         // A 4-pole ladder: (1 + jx)^4 with feedback k.
         const double k = 4.0 * res;
         double re = 1, im = 0;
         for (int i = 0; i < 4; ++i) {
            const double r = re - im * x, m = im + re * x;
            re = r, im = m;
         }
         re += k;
         return (1.0 + k) / std::sqrt(re * re + im * im);
      }
      default: {
         // Combs: a delay of one period at the cutoff, fed back.
         const double g = (0.3 + 0.68 * res) * (type == 6 ? 1.0 : -1.0);
         const double ph = 2.0 * kPi * x;
         const double re = 1.0 - g * std::cos(ph), im = g * std::sin(ph);
         return (1.0 - std::fabs(g)) / std::sqrt(re * re + im * im);
      }
      }
   }

   EditorHost &host_;
   Mapper &mapper_;
   const FlockView &sky_;
   bool live_ = false;
   float liveHz_[kFilterCount] = {}, liveRes_[kFilterCount] = {};
   int drag_ = -1, route_ = -1;
   float startX_ = 0, startY_ = 0;
   double startCut_ = 0, startRes_ = 0;
};

// The part's voices and where they sit in the stereo field: a slot per voice the part can play, lit
// while a creature holds it, then the field from left to right with each creature where its pan puts
// it, the spread X Pan gives the flock, and the output's width around it all.
class StereoView : public visage::Frame {
public:
   StereoView(EditorHost &host, const FlockView &sky) : host_(host), sky_(sky) { setIgnoresMouseEvents(true, false); }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 120.0f;
      const int part = host_.part;
      const unsigned color = kPartColors[part];
      const auto param = [&](clap_id id) { return static_cast<float>(host_.paramValue(id)); };
      const float pan = param(partId(part, kPartPan)), spread = param(partId(part, kSkyPan));
      const float widthOut = param(kOutputWidth);
      const FlockFrame &f = sky_.frame();
      // The field: L to R across, with the output's width as brackets.
      const float top = 0, fh = h - 16 * s, mid = top + fh / 2;
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, top, w, fh, 6 * s);
      c.setColor(visage::Color(0x22ffffff));
      c.fill(8 * s, mid, w - 16 * s, 1);
      c.fill(w / 2, top + 6 * s, 1, fh - 12 * s);
      const auto xOf = [&](float p) { return w / 2 + std::clamp(p, -1.0f, 1.0f) * (w / 2 - 14 * s); };
      const float half = 0.5f * std::min(widthOut, 2.0f);
      c.setColor(visage::Color(0x55ffffff));
      for (float side : {-1.0f, 1.0f}) {
         const float bx = xOf(side * std::min(1.0f, half));
         c.fill(bx - 1 * s, top + 8 * s, 2 * s, fh - 16 * s);
      }
      // The flock's spread, centred on the part's pan.
      const float l = xOf(pan - spread), r = xOf(pan + spread);
      c.setColor(glow(color, 0.16f, 1.0f));
      c.roundedRectangle(std::min(l, r) - 6 * s, mid - 12 * s, std::fabs(r - l) + 12 * s, 24 * s, 12 * s);
      c.setColor(glow(color, 0.9f, 1.2f));
      c.fill(xOf(pan) - 1 * s, mid - 16 * s, 2 * s, 32 * s);
      // Each creature of the part where it is heard.
      for (int i = 0; i < f.count; ++i) {
         const FlockFrame::Shown &cr = f.creatures[i];
         if (cr.part != part)
            continue;
         const float across = std::clamp(cr.x / kSkyAspect, 0.0f, 1.0f) * 2.0f - 1.0f;
         const float x = xOf(pan + spread * across), e = std::clamp(cr.energy, 0.2f, 1.0f);
         c.setColor(glow(color, 0.25f * e, 1.0f));
         c.circle(x - 9 * s, mid - 9 * s, 18 * s);
         c.setColor(glow(color, cr.held ? 1.0f : 0.5f, 1.6f));
         c.circle(x - 3.5f * s, mid - 3.5f * s, 7 * s);
      }
      c.setColor(visage::Color(0x88ffffff));
      c.text("L", ui::font(10 * s), visage::Font::kLeft, 6 * s, h - 14 * s, 20 * s, 14 * s);
      c.text("R", ui::font(10 * s), visage::Font::kRight, w - 26 * s, h - 14 * s, 20 * s, 14 * s);
      c.text("WIDTH", ui::font(9 * s), visage::Font::kCenter, 0, h - 14 * s, w, 14 * s);
   }

private:
   EditorHost &host_;
   const FlockView &sky_;
};

// A slot for each voice the part can play, four by four, lit while a creature holds it.
class VoiceSlotsView : public visage::Frame {
public:
   VoiceSlotsView(EditorHost &host, const FlockView &sky) : host_(host), sky_(sky) { setIgnoresMouseEvents(true, false); }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 120.0f;
      const int part = host_.part;
      const unsigned color = kPartColors[part];
      const int voices = static_cast<int>(host_.paramValue(partId(part, kPolyphony)) + 0.5);
      const FlockFrame &f = sky_.frame();
      int held = 0;
      for (int i = 0; i < f.count; ++i)
         held += f.creatures[i].part == part && f.creatures[i].held;
      const float cell = std::min(w, h - 16 * s) / 4, r = cell * 0.22f;
      const float x0 = (w - 4 * cell) / 2;
      for (int i = 0; i < 16; ++i) {
         const float cx = x0 + (i % 4 + 0.5f) * cell, cy = (i / 4 + 0.5f) * cell;
         if (i >= voices) {
            c.setColor(visage::Color(0x14ffffff));
            c.circle(cx - r, cy - r, 2 * r);
            continue;
         }
         const bool on = i < held;
         if (on) {
            c.setColor(glow(color, 0.25f, 1.0f));
            c.circle(cx - 2 * r, cy - 2 * r, 4 * r);
         }
         c.setColor(on ? glow(color, 1.0f, 1.5f) : glow(color, 0.35f, 1.0f));
         c.circle(cx - r, cy - r, 2 * r);
      }
      char text[32];
      std::snprintf(text, sizeof(text), "%d OF %d VOICES", std::min(held, voices), voices);
      c.setColor(visage::Color(0x88ffffff));
      c.text(text, ui::font(9 * s), visage::Font::kCenter, 0, h - 14 * s, w, 14 * s);
   }

private:
   EditorHost &host_;
   const FlockView &sky_;
};

// The levels, live: a bar each for A, B and the main output, lit by what is playing now, on a dB
// scale (-60 to +6), glowing as it peaks, with a tick at where its level knob is set. The part the
// panels edit is bright, the other dim.
class LevelView : public visage::Frame {
public:
   LevelView(EditorHost &host, const float (&meters)[3]) : host_(host), meters_(meters) { setIgnoresMouseEvents(true, false); }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 120.0f;
      const float top = 6 * s, bottom = h - 18 * s;
      const auto unit = [](double db) { return static_cast<float>((std::clamp(db, -60.0, 6.0) + 60.0) / 66.0); };
      const auto yOf = [&](double db) { return bottom - unit(db) * (bottom - top); };
      const double settings[3] = {host_.paramValue(partId(0, kPartLevel)), host_.paramValue(partId(1, kPartLevel)),
                                  host_.paramValue(kMasterLevel)};
      const unsigned colors[3] = {kPartColors[0], kPartColors[1], 0xffdfe6f5};
      const char *names[3] = {"A", "B", w > 100 * s ? "MASTER" : "M"};
      for (int db : {0, -12, -24, -36, -48}) {
         c.setColor(visage::Color(db == 0 ? 0x44ffffff : 0x18ffffff));
         c.fill(0, yOf(db), w, 1);
      }
      for (int i = 0; i < 3; ++i) {
         const float bw = std::min(20 * s, w * 0.16f), x = w * (0.2f + 0.3f * i) - bw / 2;
         const float lit = i == 2 || i == host_.part ? 1.0f : 0.45f;
         c.setColor(visage::Color(0x16ffffff));
         c.roundedRectangle(x, top, bw, bottom - top, 4 * s);
         const double db = meters_[i] > 1e-5f ? 20.0 * std::log10(meters_[i]) : -90.0;
         const float u = unit(db), y = yOf(db);
         if (u > 0.0f) {
            // The glow grows with the level: a wide soft halo, then the bar itself.
            for (int ring = 3; ring >= 1; --ring) {
               const float g = ring * (2.0f + 6.0f * u) * s;
               c.setColor(glow(colors[i], 0.07f * lit * u * (4 - ring), 1.0f));
               c.roundedRectangle(x - g, y - g, bw + 2 * g, bottom - y + g, 4 * s + g);
            }
            c.setColor(glow(colors[i], (0.55f + 0.45f * u) * lit, 1.0f + 0.5f * u * lit));
            c.roundedRectangle(x, y, bw, bottom - y, 4 * s);
            // Over 0 dB the top turns hot.
            if (db > 0.0) {
               c.setColor(visage::Color(0xffff5a4a));
               c.roundedRectangle(x, y, bw, yOf(0.0) - y, 3 * s);
            }
         }
         // Where its knob is set.
         const float ty = yOf(settings[i]);
         c.setColor(visage::Color(lit == 1.0f ? 0xeeffffff : 0x77ffffff));
         c.fill(x - 4 * s, ty - 1 * s, bw + 8 * s, 2 * s);
         c.setColor(visage::Color(lit == 1.0f ? 0xccffffff : 0x66ffffff));
         c.text(names[i], ui::font(9 * s), visage::Font::kCenter, x - 20 * s, h - 14 * s, bw + 40 * s, 14 * s);
      }
   }

private:
   EditorHost &host_;
   const float (&meters_)[3];
};

// An LFO's shape `cycles` into its run, -1..1: the panel's guess at the engine's, for its LEDs and
// pictures.
float lfoWave(int shape, double cycles) {
   const float ph = static_cast<float>(cycles - std::floor(cycles));
   switch (shape) {
   case 1: return 1.0f - 4.0f * std::fabs(ph - 0.5f); // triangle
   case 2: return 2.0f * ph - 1.0f;                   // saw
   case 3: return ph < 0.5f ? 1.0f : -1.0f;           // square
   case 4: return std::sin(static_cast<float>(std::floor(cycles)) * 12.9898f) > 0.0f ? 0.7f : -0.7f; // S&H
   case 5: return 0.6f * std::sin(2.0f * kPi * ph) + 0.4f * std::sin(2.0f * kPi * ph * 0.37f + 1.0f); // smooth
   default: return std::sin(2.0f * kPi * ph); // sine
   }
}

// A plain LFO drawn: two cycles of its shape, from its phase, unipolar ones from the floor, with a
// dot running along it at its rate.
class LfoView : public visage::Frame {
public:
   LfoView(EditorHost &host, const FlockView &sky, int lfo, unsigned color, const float &time)
       : host_(host), sky_(sky), lfo_(lfo), color_(color), time_(time) {
      setIgnoresMouseEvents(true, false);
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 127.0f;
      const auto param = [&](LfoParam p) { return sky_.value(host_.part, lfoParam(lfo_, p)); };
      const int shape = static_cast<int>(param(kLfoShape) + 0.5);
      const double phase = param(kLfoPhase) / 360.0;
      const bool unipolar = static_cast<int>(param(kLfoPolarity) + 0.5) == 1;
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      const float x0 = 8 * s, x1 = w - 8 * s, top = 10 * s, bottom = h - 10 * s;
      const auto yOf = [&](float v) {
         const float u = unipolar ? 0.5f * v + 0.5f : 0.5f + 0.5f * v;
         return bottom - u * (bottom - top);
      };
      c.setColor(visage::Color(0x18ffffff));
      c.fill(x0, unipolar ? bottom : (top + bottom) / 2, x1 - x0, 1);
      constexpr int kPoints = 160;
      for (int pass = 0; pass < 2; ++pass) {
         c.setColor(pass == 0 ? glow(color_, 0.3f, 1.0f) : glow(color_, 1.0f, 1.2f));
         float px = x0, py = yOf(lfoWave(shape, phase));
         for (int i = 1; i <= kPoints; ++i) {
            const double t = 2.0 * i / kPoints;
            const float nx = x0 + (x1 - x0) * static_cast<float>(t / 2.0), ny = yOf(lfoWave(shape, t + phase));
            c.segment(px, py, nx, ny, (pass == 0 ? 6.0f : 2.0f) * s, true);
            px = nx, py = ny;
         }
      }
      // The dot: where the LFO is now, free running at its rate.
      const double run = time_ * lfoHz(param(kLfoRate));
      const double at = run - 2.0 * std::floor(run / 2.0);
      const float dx = x0 + (x1 - x0) * static_cast<float>(at / 2.0), dy = yOf(lfoWave(shape, at + phase)), r = 5 * s;
      c.setColor(glow(color_, 1.0f, 1.5f));
      c.circle(dx - r, dy - r, 2 * r);
   }

private:
   EditorHost &host_;
   const FlockView &sky_;
   int lfo_;
   unsigned color_;
   const float &time_;
};

// An envelope in Shape mode drawn: its contour as the engine plays it, warped, bent and maybe
// reversed, one cycle across the width, with how often it repeats in the corner and a dot riding
// it for every sounding voice. Drag sideways to warp it and up or down to bend it (shift for fine
// moves); double-click puts both back. In MAP with a source armed, the drag sets how much the source
// warps it (sideways) or bends it (up or down) instead, and the shape as the source at its fullest
// would take it shows over it in the source's colour; a right-click removes both routes.
class ShapeView : public ui::XYPad {
public:
   ShapeView(const FlockView &sky, Mapper &mapper, int part, int env, ui::ValueRange warp, ui::ValueRange bend)
       : XYPad(warp, bend), sky_(sky), mapper_(mapper), part_(part), env_(env),
         dests_{destinationIndex(shapeParam(env, kShapeWarp)), destinationIndex(shapeParam(env, kShapeBend))} {}

   void setTheme(const ui::Theme &theme) {
      theme_ = &theme;
      popup_.setTheme(theme);
      XYPad::setTheme(theme);
   }
   void setAccent(unsigned color) {
      accent_ = color;
      XYPad::setAccent(color);
   }
   // Each voice's place in the cycle and its level.
   void setPlayheads(std::vector<std::pair<float, float>> heads) {
      if (heads != heads_) {
         heads_ = std::move(heads);
         redraw();
      }
   }

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = *theme_;
      const float w = width(), h = height(), s = h / 66.0f;
      c.setColor(t.knobDark);
      c.roundedRectangle(0, 0, w, h, 5 * s);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, w, h, 5 * s, 1.0f);
      const float x0 = 6 * s, x1 = w - 6 * s, top = 6 * s, bottom = h - 6 * s;
      c.setColor(t.track);
      for (int i = 1; i < 4; ++i)
         c.fill(x0, top + (bottom - top) * i / 4.0f, x1 - x0, 1.0f);

      const auto param = [&](ShapeParam p) { return sky_.value(part_, shapeParam(env_, p)); };
      const int index = static_cast<int>(param(kShapeIndex) + 0.5);
      dsp::Envelope::Settings set;
      set.shapeMode = true;
      set.table = shapeTable(index);
      set.points = kShapePoints;
      set.warp = static_cast<float>(x().modulated);
      set.bend = static_cast<float>(y().modulated);
      set.reverse = static_cast<int>(param(kShapeReverse) + 0.5) == 1;
      contour_.set(set);
      const int columns = std::max(2, static_cast<int>(x1 - x0));
      const auto yOf = [&](float level) { return bottom - std::clamp(level, 0.0f, 1.0f) * (bottom - top); };

      // Where the armed source takes it, under the shape itself.
      const int armed = mapper_.mapping() ? mapper_.armedSource() : kSourceOff;
      if (armed != kSourceOff) {
         c.setColor(glow(sourceColor(armed), 0.55f, 1.0f));
         c.roundedRectangleBorder(0, 0, w, h, 5 * s, 1.4f);
         float moved[2] = {0.0f, 0.0f};
         for (int a = 0; a < 2; ++a) {
            rings_.clear();
            if (dests_[a] >= 0)
               mapper_.rings(part_, dests_[a], rings_);
            for (const Mapper::Ring &r : rings_)
               if (r.source == armed)
                  moved[a] += static_cast<float>(r.amount);
         }
         if (moved[0] != 0.0f || moved[1] != 0.0f) {
            dsp::Envelope::Settings far = set;
            far.warp = std::clamp(set.warp + moved[0] * static_cast<float>(x().range.max - x().range.min), -1.0f, 1.0f);
            far.bend = std::clamp(set.bend + moved[1] * static_cast<float>(y().range.max - y().range.min), -1.0f, 1.0f);
            reach_.set(far);
            c.setColor(glow(sourceColor(armed), 1.0f, 1.6f));
            float px = x0, py = yOf(reach_.shapeLevel(0.0f));
            for (int i = 1; i <= columns; i += 2) {
               const float f = static_cast<float>(i) / columns;
               const float nx = x0 + (x1 - x0) * f, ny = yOf(reach_.shapeLevel(f));
               c.segment(px, py, nx, ny, 1.2f * s, true);
               px = nx, py = ny;
            }
         }
      }
      c.setColor((accent_ & 0x00ffffff) | 0x30000000);
      float px = x0, py = yOf(contour_.shapeLevel(0.0f));
      for (int i = 1; i <= columns; ++i) {
         const float f = static_cast<float>(i) / columns;
         const float nx = x0 + (x1 - x0) * f, ny = yOf(contour_.shapeLevel(f));
         c.fill(px, std::min(py, ny), nx - px, bottom - std::min(py, ny));
         px = nx, py = ny;
      }
      c.setColor(accent_);
      px = x0, py = yOf(contour_.shapeLevel(0.0f));
      for (int i = 1; i <= columns; i += 2) {
         const float f = static_cast<float>(i) / columns;
         const float nx = x0 + (x1 - x0) * f, ny = yOf(contour_.shapeLevel(f));
         c.segment(px, py, nx, ny, 2.0f * s, true);
         px = nx, py = ny;
      }
      c.segment(px, py, x1, yOf(contour_.shapeLevel(1.0f)), 2.0f * s, true);

      // Its bank, and how often it plays.
      const int repeat = static_cast<int>(param(kShapeRepeat) + 0.5);
      c.setColor(kDim);
      c.text(upper(shapeBanks()[index / kShapeBankSize].name), ui::font(9 * s), visage::Font::kLeft, x0 + 3 * s, top,
             w / 2, 12 * s);
      c.text(upper(kRepeatNames[std::clamp(repeat, 0, static_cast<int>(kRepeatNames.size()) - 1)]), ui::font(9 * s),
             visage::Font::kRight, w / 2, top, x1 - w / 2 - 3 * s, 12 * s);

      const float dot = 6 * s, glowSize = dot * 2.2f;
      for (const auto &[phase, level] : heads_) {
         const float ax = x0 + (x1 - x0) * std::clamp(phase, 0.0f, 1.0f), ay = yOf(level);
         c.setColor((accent_ & 0x00ffffff) | 0x30000000);
         c.circle(ax - glowSize * 0.5f, ay - glowSize * 0.5f, glowSize);
         c.setColor((accent_ & 0x00ffffff) | 0x60000000);
         c.circle(ax - glowSize * 0.32f, ay - glowSize * 0.32f, glowSize * 0.64f);
         c.setColor(t.modulation);
         c.circle(ax - dot * 0.3f, ay - dot * 0.3f, dot * 0.6f);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (dragging_)
         return;
      if (mapper_.mapping()) {
         if (e.isRightButton()) {
            for (int d : dests_)
               if (d >= 0)
                  mapper_.unmap(part_, d);
            redraw();
         }
         else if (e.isLeftButton() && mapper_.armedSource() != kSourceOff) {
            mapping_ = true, axis_ = -1, route_ = -1;
            from_ = e.position;
         }
         return;
      }
      if (e.shouldTriggerPopup()) {
         XYPad::mouseDown(e);
         return;
      }
      if (!e.isLeftButton())
         return;
      if (e.repeatClickCount() == 2) {
         for (Axis *a : {&x(), &y()}) {
            if (a->onBeginEdit)
               a->onBeginEdit();
            a->set(a->range.defaultValue);
            if (a->onEndEdit)
               a->onEndEdit();
         }
         redraw();
         return;
      }
      dragging_ = true;
      grab(e);
      for (Axis *a : {&x(), &y()})
         if (a->onBeginEdit)
            a->onBeginEdit();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (mapping_) {
         const float dx = e.position.x - from_.x, dy = from_.y - e.position.y;
         // The way it first moves picks the route: sideways for Warp, up or down for Bend.
         if (axis_ < 0) {
            if (std::max(std::fabs(dx), std::fabs(dy)) < 4.0f)
               return;
            axis_ = std::fabs(dx) >= std::fabs(dy) ? 0 : 1;
            route_ = dests_[axis_] >= 0 ? mapper_.routeFor(part_, dests_[axis_]) : -1;
            if (route_ < 0)
               return;
            mapper_.beginRoute(part_, route_);
            startX_ = mapper_.routeAmount(part_, route_);
         }
         if (route_ < 0)
            return;
         const float scale = e.isShiftDown() ? 0.15f : 1.0f;
         const float moved = axis_ == 0 ? dx / std::max(1.0f, width()) : dy / std::max(1.0f, height());
         mapper_.setRouteAmount(part_, route_, std::clamp(startX_ + moved * scale, -1.0, 1.0));
         redraw();
         return;
      }
      if (!dragging_)
         return;
      // Shift slows it down, from where it is now.
      if (e.isShiftDown() != fine_)
         grab(e);
      const float scale = fine_ ? 0.15f : 1.0f;
      const float dx = (e.position.x - from_.x) / std::max(1.0f, width());
      const float dy = (from_.y - e.position.y) / std::max(1.0f, height());
      x().set(startX_ + dx * scale * (x().range.max - x().range.min));
      y().set(startY_ + dy * scale * (y().range.max - y().range.min));
      popup_.show(*this, "WARP " + x().text(x().value) + "   BEND " + y().text(y().value));
      redraw();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (mapping_) {
         if (route_ >= 0)
            mapper_.endRoute(part_, route_);
         mapping_ = false, route_ = -1;
         redraw();
         return;
      }
      if (dragging_)
         for (Axis *a : {&x(), &y()})
            if (a->onEndEdit)
               a->onEndEdit();
      dragging_ = false;
      popup_.hide();
   }

private:
   void grab(const visage::MouseEvent &e) {
      from_ = e.position;
      startX_ = x().value;
      startY_ = y().value;
      fine_ = e.isShiftDown();
   }

   const FlockView &sky_;
   Mapper &mapper_;
   int part_, env_;
   int dests_[2]; // Warp's and Bend's places in destinations()
   const ui::Theme *theme_ = &ui::defaultTheme();
   unsigned accent_ = 0xffffffff;
   dsp::Envelope contour_, reach_;
   std::vector<Mapper::Ring> rings_;
   bool mapping_ = false;
   int axis_ = -1, route_ = -1;
   std::vector<std::pair<float, float>> heads_;
   bool dragging_ = false, fine_ = false;
   visage::Point from_;
   double startX_ = 0, startY_ = 0;
   ui::ValuePopup popup_;
};

// The FM as what comes out: a C4 run through the three oscillators the way the engine does it,
// two cycles drawn, with a chip under it for each route that is on.
class FmView : public visage::Frame {
public:
   FmView(EditorHost &host, const FlockView &sky) : host_(host), sky_(sky) { setIgnoresMouseEvents(true, false); }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      const float s = h / 290.0f;
      const auto param = [&](clap_id offset) { return static_cast<float>(sky_.value(host_.part, offset)); };
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      c.setColor(visage::Color(0x88ffffff));
      c.text("WHAT COMES OUT, AT C4", ui::font(9 * s), visage::Font::kLeft, 10 * s, 6 * s, w, 16 * s);

      // The run: each oscillator at its pitch, as the engine's sample loop modulates them.
      constexpr float kRate = 48000, kNote = 60;
      float inc[kOscCount], shape[kOscCount], level[kOscCount], amount[kOscCount][kOscCount];
      int wave[kOscCount];
      for (int o = 0; o < kOscCount; ++o) {
         const int mode = static_cast<int>(param(oscParam(o, kOscPitchMode)) + 0.5f);
         const float fine = param(oscParam(o, kOscFine));
         float hz;
         if (mode == 0)
            hz = 440.0f * std::exp2((kNote + param(oscParam(o, kOscCoarse)) + fine / 100.0f - 69.0f) / 12.0f);
         else if (mode == 1)
            hz = 440.0f * std::exp2((kNote + fine / 100.0f - 69.0f) / 12.0f) * param(oscParam(o, kOscRatio));
         else
            hz = static_cast<float>(fixedHz(param(oscParam(o, kOscFixed)))) * std::exp2(fine / 1200.0f);
         inc[o] = std::clamp(hz, 0.0f, 0.45f * kRate) / kRate;
         wave[o] = static_cast<int>(param(oscParam(o, kOscWave)) + 0.5f);
         // Noise and Random have no one shape; a sine stands in.
         if (wave[o] >= dsp::kWhite)
            wave[o] = dsp::kSine;
         shape[o] = param(oscParam(o, kOscShape));
         level[o] = param(mixParam(kMixLevel, o));
         for (int to = 0; to < kOscCount; ++to)
            amount[o][to] = param(fmCell(o, to));
      }
      const bool expFm = static_cast<int>(param(kFmBase + kFmType) + 0.5f) == 1;
      const int cycle = static_cast<int>(1.0f / std::max(inc[0], 1e-4f));
      constexpr int kWarm = 4; // cycles run before the drawn two, so feedback settles
      double phase[kOscCount] = {};
      float out[kOscCount] = {}, last[kOscCount] = {};
      std::vector<float> y;
      y.reserve(2 * cycle + 1);
      float top = 1e-6f;
      for (int i = 0; i < (kWarm + 2) * cycle + 1; ++i) {
         float now[kOscCount] = {out[0], out[1], out[2]};
         for (int o = kOscCount - 1; o >= 0; --o) {
            float m = 0;
            for (int from = 0; from < kOscCount; ++from)
               m += amount[from][o] * (from == o ? 0.25f * (out[o] + last[o]) : 2.0f * now[from]);
            float step = inc[o], ph;
            if (expFm) {
               step = std::min(step * std::exp2(std::clamp(2.0f * m, -4.0f, 4.0f)), 0.45f);
               ph = static_cast<float>(phase[o]);
            }
            else
               ph = static_cast<float>(phase[o]) + m - std::floor(static_cast<float>(phase[o]) + m);
            now[o] = dsp::wave(wave[o], ph, inc[o], shape[o]);
            last[o] = out[o];
            phase[o] += step;
            phase[o] -= std::floor(phase[o]);
         }
         float sum = 0;
         for (int o = 0; o < kOscCount; ++o) {
            out[o] = now[o];
            sum += level[o] * now[o];
         }
         if (i >= kWarm * cycle) {
            y.push_back(sum);
            top = std::max(top, std::fabs(sum));
         }
      }

      const unsigned col = kPartColors[host_.part];
      const float x0 = 10 * s, x1 = w - 10 * s, mid = h * 0.42f, amp = h * 0.3f;
      c.setColor(visage::Color(0x18ffffff));
      c.fill(x0, mid, x1 - x0, 1);
      const auto at = [&](size_t i, float &px, float &py) {
         px = x0 + (x1 - x0) * i / (y.size() - 1);
         py = mid - y[i] / top * amp;
      };
      for (int pass = 0; pass < 2; ++pass) {
         c.setColor(pass == 0 ? glow(col, 0.3f, 1.0f) : glow(col, 1.0f, 1.3f));
         const float thick = (pass == 0 ? 7.0f : 2.0f) * s;
         float px0, py0, px1, py1;
         at(0, px0, py0);
         for (size_t i = 1; i < y.size(); ++i) {
            at(i, px1, py1);
            c.segment(px0, py0, px1, py1, thick, true);
            px0 = px1, py0 = py1;
         }
      }

      // The routes that are on, biggest first.
      struct Route {
         int from, to;
         float amount;
      };
      std::vector<Route> routes;
      for (int from = 0; from < kOscCount; ++from)
         for (int to = 0; to < kOscCount; ++to)
            if (amount[from][to] >= 0.005f)
               routes.push_back({from, to, amount[from][to]});
      std::stable_sort(routes.begin(), routes.end(), [](const Route &a, const Route &b) { return a.amount > b.amount; });
      const float cw = (w - 20 * s - 2 * 6 * s) / 3, ch = 20 * s;
      if (routes.empty()) {
         c.setColor(visage::Color(0x88ffffff));
         c.text("NO FM: TURN UP A KNOB IN THE GRID", ui::font(9 * s), visage::Font::kCenter, 0, h - 44 * s, w, ch);
      }
      for (size_t i = 0; i < routes.size() && i < 6; ++i) {
         const Route &r = routes[i];
         const float cx = 10 * s + (i % 3) * (cw + 6 * s), cy = h - (i < 3 ? 52 : 28) * s;
         const unsigned rc = kOscColors[r.from];
         c.setColor(glow(rc, 0.2f, 1.0f));
         c.roundedRectangle(cx, cy, cw, ch, ch / 2);
         c.setColor(glow(rc, 1.0f, 1.2f));
         c.roundedRectangleBorder(cx, cy, cw, ch, ch / 2, 1.5f * s);
         const std::string name = r.from == r.to ? "FB " + std::to_string(r.from + 1)
                                                 : std::to_string(r.from + 1) + " > " + std::to_string(r.to + 1);
         c.text(name + "  " + std::to_string(static_cast<int>(r.amount * 100 + 0.5f)) + "%", ui::font(9 * s),
                visage::Font::kCenter, cx, cy, cw, ch);
      }
   }

private:
   EditorHost &host_;
   const FlockView &sky_;
};

class Panel : public ui::DesignFrame, public visage::EventTimer, public Mapper {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      view_ = &add<FlockView>({0, kHeaderHeight, kDesignWidth, kDesignHeight - kHeaderHeight}, host, kDesignWidth);
      // The sky fills the panel below the header, so its right-click opens the UI size menu the
      // panel's own background would.
      view_->onBackgroundMenu = [this](visage::Point p) { showZoomMenu(*view_, p); };
      buildHeader();
      for (int part = 0; part < kPartCount; ++part) {
         part_ = part;
         group_ = kSynth;
         buildPart(part);
         buildRoutes(part);
      }
      part_ = -1;
      group_ = kSynth;
      buildShared();
      buildRouteControls();
      showRoutes(host_.routesOpen);
      showPart(host_.part);
      setView(host_.view);
      last_ = std::chrono::steady_clock::now();
      startTimer(16);
   }

   // One animation frame; the timer calls it with the real time since the last one.
   void step(float dt) {
      time_ += dt;
      host_.notes(notes_);
      view_->shownPart = host_.part;
      view_->hint = !mapping_ ? std::string()
                    : armed_ != kSourceOff
                        ? "Now drag any knob to set how much " + upper(kSourceNames[armed_]) + " moves it"
                        : "MAP: touch a source, then drag a knob";
      view_->hintColor = armed_ != kSourceOff ? sourceColor(armed_) : kMapColor;
      view_->advance(notes_, dt);
      showLiveModulation();
      // The chips' levels: the newest note's touch, and the shown species' newest creature.
      float touch[kTouchCount] = {};
      if (!notes_.empty()) {
         const EditorHost::Note &n = notes_.back();
         touch[0] = n.velocity;
         touch[1] = n.pressure;
         touch[2] = std::min(1.0f, std::fabs(n.bend) / 2.0f);
         touch[3] = n.timbre;
         touch[4] = n.release;
      }
      runEnvelopes(dt);
      showPlayheads();
      // The level bars: each frame's peak, falling back at about 26 dB a second.
      const float fall = std::exp(-3.0f * dt);
      for (int i = 0; i < 3; ++i)
         meters_[i] = std::max(view_->live() ? view_->frame().meter[i] : 0.0f, meters_[i] * fall);
      const CreatureSources src = view_->sources(host_.part);
      const float flock[] = {src.speed, src.nearest, src.heading, 0.5f + 0.5f * src.turn};
      const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.0f);
      for (SourceChip *chip : chips_) {
         const int s = chip->source();
         if (s >= kSourceVelocity && s < kSourceVelocity + kTouchSourceCount)
            chip->setLevel(touch[s - kSourceVelocity]);
         else if (s >= kSourceSpeed && s <= kSourceTurn)
            chip->setLevel(flock[s - kSourceSpeed], s == kSourceTurn);
         else if (s >= kSourceEnv1 && s < kSourceEnv1 + kEnvCount)
            chip->setLevel(view_->live() ? view_->envLevel(host_.part, s - kSourceEnv1) : envLevel_[s - kSourceEnv1]);
         else if (s >= kSourceLfo1 && s < kSourceLfo1 + kLfoCount - kMotionCount) {
            // The engine's LFO is already unipolar or bipolar by its Polarity; the panel's guess is
            // too. The chip shows either across its width.
            const int l = kMotionCount + s - kSourceLfo1;
            const float v = view_->live() ? view_->lfoLevel(host_.part, l) : lfoValue(l);
            const bool unipolar = static_cast<int>(value(host_.part, lfoParam(l, kLfoPolarity)) + 0.5) == 1;
            chip->setLevel(unipolar ? v : 0.5f + 0.5f * v, !unipolar);
         }
         chip->setState(mapping_, mapping_ && s == armed_, pulse);
      }
      if (!(host_.view == kSkyOnly)) {
         refreshPitch();
         refreshEnvRanges();
         if (envModeSum() != envModes_)
            refresh();
      }
      if (host_.view != kSkyOnly)
         for (visage::Frame *d : drawings_)
            if (d->isVisible())
               d->redraw();
      // Rings follow the routes, which the host or the route list can change too.
      const double sum = routeChecksum();
      if (sum != routeSum_) {
         routeSum_ = sum;
         if (!(host_.view == kSkyOnly))
            layoutRoutes();
         for (MapKnob *k : mapKnobs_)
            k->redraw();
         for (visage::Frame *f : mapped_)
            f->redraw();
      }
   }

   // Each mappable knob's live line: past the host's modulation, to where its part's newest voice
   // has it now, while a route or the host's per-note modulation moves it.
   void showLiveModulation() {
      const FlockFrame &f = view_->frame();
      for (MapKnob *k : mapKnobs_) {
         const int d = k->destination();
         const bool on = view_->live() && d >= 0 && f.moved[k->part()][d];
         k->setLiveModulation(on ? f.dest[k->part()][d] - k->modulatedValue() : 0.0, 0xffffffff);
      }
   }

   void timerCallback() override {
      const auto now = std::chrono::steady_clock::now();
      const float dt = std::chrono::duration<float>(now - last_).count();
      last_ = now;
      step(std::clamp(dt, 0.0f, 0.05f));
   }

   // Pulls every control's value from the host now, as the bindings' timer does.
   void syncControls() { bindings_.sync(); }

   void showPart(int part) {
      host_.part = std::clamp(part, 0, kPartCount - 1);
      partSwitch_->setSelected(host_.part);
      partSwitch_->setAccent(kPartColors[host_.part]);
      refresh();
   }
   void showRoutes(bool open) {
      host_.routesOpen = open;
      routesButton_->setOn(open);
      refresh();
   }
   // Switches the view: the synth's panels beside the sky, or the sky alone filling the window. The
   // view switch stays, top right, in both.
   // Opens one of the tabs under the sky.
   void showVoiceTab(int tab) {
      voiceTab_ = tab;
      voiceTabs_->select(tab);
      refresh();
   }

   void setView(int view) {
      host_.view = std::clamp(view, 0, 1);
      viewSwitch_->setSelected(host_.view);
      if (host_.view == kSynthView) {
         move(*view_, {0, kHeaderHeight, kDesignWidth, kDesignHeight - kHeaderHeight});
         view_->skyLeft = kSkyX;
         view_->skyTop = kSkyY - kHeaderHeight;
         view_->skyHeight = kSkyH;
      }
      else {
         // The sky as big as the window allows, centred.
         const float h = std::min(kDesignHeight - 2 * kCleanMargin, (kDesignWidth - 2 * kCleanMargin) / kSkyAspect);
         move(*view_, {0, 0, kDesignWidth, kDesignHeight});
         view_->skyLeft = (kDesignWidth - h * kSkyAspect) / 2;
         view_->skyTop = (kDesignHeight - h) / 2;
         view_->skyHeight = h;
      }
      if (host_.view == kSkyOnly && mapping_)
         setMapping(false, kSourceOff);
      refresh();
   }
   // MAP on or off, with `source` armed.
   void setMapping(bool on, int source) {
      mapping_ = on;
      armed_ = on ? source : kSourceOff;
      mapButton_->setOn(on);
      for (MapKnob *k : mapKnobs_)
         k->redraw();
      for (visage::Frame *f : mapped_)
         f->redraw();
   }

   void draw(visage::Canvas &c) override {
      if ((host_.view == kSkyOnly))
         return;
      const float s = scale();
      c.setColor(0xff070910);
      c.fill(0, 0, width(), kHeaderHeight * s);
      c.setColor(visage::Color(0x22ffffff));
      c.fill(0, (kHeaderHeight - 1) * s, width(), 1 * s);
      // Logo: three creatures, each trailing ink.
      for (int i = 0; i < 3; ++i) {
         const unsigned col = i == 2 ? kInks[1][0] : kInks[0][i];
         const float bx = 22.0f + 12.0f * i, by = i == 1 ? 18.0f : i == 2 ? 34.0f : 27.0f;
         for (int k = 0; k < 7; ++k) {
            const float t = k / 7.0f, r = (0.8f + 1.8f * t) * s;
            c.setColor(glow(col, 0.1f + 0.45f * t, 1.0f));
            c.circle((bx - 15.0f + 14.0f * t) * s - r, (by + 6.0f * (1.0f - t) * (1.0f - t)) * s - r, 2 * r);
         }
         c.setColor(col);
         c.circle((bx - 3.5f) * s, (by - 3.5f) * s, 7 * s);
      }
      c.setColor(kPartColors[0]);
      c.text("J", ui::font(24 * s), visage::Font::kLeft, 62 * s, 6 * s, 30 * s, 40 * s);
      c.setColor(kText);
      c.text("Synth", ui::font(24 * s), visage::Font::kLeft, 75 * s, 6 * s, 90 * s, 40 * s);
      c.setColor(kDim);
      // A newer release's notice takes the tagline's place.
      if (!update_->isVisible())
         c.text("flocking synth", ui::font(12 * s), visage::Font::kLeft, 146 * s, 6 * s, 120 * s, 40 * s);
   }

   // ---- Mapper ----

   bool mapping() const override { return mapping_; }
   int armedSource() const override { return armed_; }

   void rings(int part, int dest, std::vector<Ring> &out) const override {
      for (int r = 0; r < kRouteCount; ++r) {
         const int source = static_cast<int>(value(part, routeParam(r, kRouteSource)) + 0.5);
         if (source != kSourceOff && static_cast<int>(value(part, routeParam(r, kRouteDestination)) + 0.5) == dest)
            out.push_back({r, source, value(part, routeParam(r, kRouteAmount))});
      }
   }
   int routeFor(int part, int dest) override {
      std::vector<Ring> found;
      rings(part, dest, found);
      for (const Ring &r : found)
         if (r.source == armed_)
            return r.route;
      for (int r = 0; r < kRouteCount; ++r)
         if (static_cast<int>(value(part, routeParam(r, kRouteSource)) + 0.5) == kSourceOff) {
            set(part, routeParam(r, kRouteSource), armed_);
            set(part, routeParam(r, kRouteDestination), dest);
            set(part, routeParam(r, kRouteAmount), 0.0);
            return r;
         }
      return -1;
   }
   double routeAmount(int part, int route) const override { return value(part, routeParam(route, kRouteAmount)); }
   void beginRoute(int part, int route) override { host_.beginEdit(partId(part, routeParam(route, kRouteAmount))); }
   void setRouteAmount(int part, int route, double amount) override {
      host_.setParam(partId(part, routeParam(route, kRouteAmount)), amount);
   }
   void endRoute(int part, int route) override {
      host_.endEdit(partId(part, routeParam(route, kRouteAmount)));
      if (std::fabs(routeAmount(part, route)) < 0.005)
         set(part, routeParam(route, kRouteSource), kSourceOff);
   }
   void unmap(int part, int dest) override {
      std::vector<Ring> found;
      rings(part, dest, found);
      for (const Ring &r : found)
         if (armed_ == kSourceOff || r.source == armed_) {
            set(part, routeParam(r.route, kRouteSource), kSourceOff);
            set(part, routeParam(r.route, kRouteAmount), 0.0);
         }
   }

private:
   // Where a widget shows: always, in the synth view, on one tab of a panel there, or in the route
   // list.
   // Which of an envelope's modes a widget shows in: the ADSR editor, or Shape mode's controls
   // (some only while its time is free or synced, or while it waits in a chain).
   struct EnvShows {
      int env = -1;
      enum When { kAdsr, kShape, kFree, kSynced, kChained } when = kAdsr;
   };
   enum Group {
      kAlways,
      kSynth,
      kOscPage,
      kFmPage,
      kEnvPage,
      kLfoPage,
      kVoicePage,
      kFlockPage,
      kMotionPage,
      kTouchPage,
      kRoutes
   };
   struct Placed {
      visage::Frame *frame;
      int part; // -1 for both
      Group group;
      EnvShows env;
   };
   struct PitchKnobs {
      int part, osc;
      ui::Knob *coarse, *ratio, *fixed;
      Group group;
   };

   double value(int part, clap_id offset) const { return host_.paramValue(partId(part, offset)); }

   // The shown part's LFO `l` now, -1..1, free running from its rate, shape and phase, for its LED.
   float lfoValue(int l) const {
      const int part = host_.part;
      const double cycles = time_ * lfoHz(value(part, lfoParam(l, kLfoRate))) + value(part, lfoParam(l, kLfoPhase)) / 360.0;
      float v = lfoWave(static_cast<int>(value(part, lfoParam(l, kLfoShape)) + 0.5), cycles);
      if (static_cast<int>(value(part, lfoParam(l, kLfoPolarity)) + 0.5) == 1)
         v = 0.5f * v + 0.5f; // unipolar
      return v;
   }

   // The shown part's envelopes, as the newest note would run them, for their LEDs: from the top
   // when a new note arrives, and released when the last one lets go.
   void runEnvelopes(float dt) {
      const int newest = notes_.empty() ? -1 : notes_.back().note;
      if (newest >= 0 && newest != envNote_)
         envTime_ = 0.0f;
      envNote_ = newest;
      envTime_ += dt;
      for (int e = 0; e < kEnvCount; ++e) {
         const auto sec = [&](EnvParam p) { return static_cast<float>(timeMs(value(host_.part, envParam(e, p)))) / 1000.0f; };
         const float sustain = static_cast<float>(value(host_.part, envParam(e, kEnvSustain)));
         float &level = envLevel_[e];
         if (newest < 0) {
            level = std::max(0.0f, level - dt * std::max(level, 0.05f) / std::max(sec(kEnvRelease), 0.005f));
            continue;
         }
         if (shapeMode(host_.part, e)) {
            // A shape: its contour along its time, repeated as it would be, holding its end after.
            const auto param = [&](ShapeParam p) { return value(host_.part, shapeParam(e, p)); };
            dsp::Envelope::Settings set;
            set.shapeMode = true;
            set.table = shapeTable(static_cast<int>(param(kShapeIndex) + 0.5));
            set.points = kShapePoints;
            set.warp = static_cast<float>(param(kShapeWarp));
            set.bend = static_cast<float>(param(kShapeBend));
            set.reverse = static_cast<int>(param(kShapeReverse) + 0.5) == 1;
            ledShape_.set(set);
            const float time = static_cast<int>(param(kShapeSync) + 0.5) == 1
                                   ? static_cast<float>(shapeBeats(static_cast<int>(param(kShapeDivision) + 0.5)) * 0.5)
                                   : static_cast<float>(shapeMs(param(kShapeTime)) / 1000.0);
            const int repeat = static_cast<int>(param(kShapeRepeat) + 0.5);
            const float cycles = envTime_ / std::max(time, 1e-3f);
            const bool ends = repeat < kRepeatHeld && cycles >= repeatCount(repeat);
            level = ledShape_.shapeLevel(ends ? 1.0f : cycles - std::floor(cycles));
            continue;
         }
         const float t = envTime_ - sec(kEnvDelay), a = sec(kEnvAttack), hold = sec(kEnvHold), d = sec(kEnvDecay);
         if (t < 0.0f)
            level = 0.0f;
         else if (t < a)
            level = t / std::max(a, 1e-4f);
         else if (t < a + hold)
            level = 1.0f;
         else
            level = sustain + (1.0f - sustain) * std::max(0.0f, 1.0f - (t - a - hold) / std::max(d, 1e-4f));
      }
   }
   // A glowing dot on each envelope for every sounding voice of its part, where the engine has it.
   void showPlayheads() {
      const FlockFrame &f = view_->frame();
      for (int part = 0; part < kPartCount; ++part)
         for (int e = 0; e < kEnvCount; ++e) {
            if (!envelopes_[part][e])
               continue;
            heads_.clear();
            shapeHeads_.clear();
            for (int i = 0; view_->live() && i < f.playheadCount; ++i) {
               const FlockFrame::Playhead &p = f.playheads[i];
               if (p.part != part || p.stage[e] < 0)
                  continue;
               // Stage 4 is a shape's: its place in the cycle, and its level.
               if (p.stage[e] == 4)
                  shapeHeads_.push_back({p.from[e], p.level[e]});
               else
                  heads_.push_back({static_cast<ui::EnvelopeEditor::Stage>(p.stage[e]), p.level[e], p.from[e]});
            }
            envelopes_[part][e]->setPlayheads(heads_);
            if (shapes_[part][e])
               shapes_[part][e]->setPlayheads(shapeHeads_);
         }
   }
   // One whole edit, as if the user made it.
   void set(int part, clap_id offset, double v) {
      const clap_id id = partId(part, offset);
      host_.beginEdit(id);
      host_.setParam(id, v);
      host_.endEdit(id);
   }
   double routeChecksum() const {
      double sum = 0.0;
      for (int part = 0; part < kPartCount; ++part)
         for (int r = 0; r < kRouteCount; ++r)
            sum += (r + 1) * (part + 1) *
                   (value(part, routeParam(r, kRouteSource)) * 7.0 + value(part, routeParam(r, kRouteDestination)) * 131.0 +
                    value(part, routeParam(r, kRouteAmount)));
      return sum;
   }

   void refresh() {
      envModes_ = envModeSum();
      for (const Placed &p : placed_)
         p.frame->setVisible(shows(p.part, p.group) && envShows(p.part, p.env));
      refreshPitch();
      refreshEnvRanges();
      if (routeInfo_ && host_.view != kSkyOnly)
         layoutRoutes();
      redraw();
   }
   // Whether a widget of `part` in `group` shows in the view now.
   bool shows(int part, Group group) const {
      if ((part >= 0 && part != host_.part) || host_.view == kSkyOnly)
         return false;
      switch (group) {
      case kSynth: return true;
      case kOscPage: return oscTab_ == 0;
      case kFmPage: return oscTab_ == 1;
      case kEnvPage: return envTab_ == 0;
      case kLfoPage: return envTab_ == 1;
      case kVoicePage: return voiceTab_ == 0;
      case kFlockPage: return voiceTab_ == 1;
      case kMotionPage: return voiceTab_ == 2;
      case kTouchPage: return voiceTab_ == 3;
      case kRoutes: return host_.routesOpen;
      default: return true;
      }
   }
   // Whether an envelope widget of `part` shows for its envelope's mode now.
   bool envShows(int part, EnvShows e) const {
      if (e.env < 0 || part < 0)
         return true;
      const bool shape = static_cast<int>(value(part, envParam(e.env, kEnvMode)) + 0.5) == kModeShape;
      const bool synced = static_cast<int>(value(part, shapeParam(e.env, kShapeSync)) + 0.5) == 1;
      switch (e.when) {
      case EnvShows::kAdsr: return !shape;
      case EnvShows::kShape: return shape;
      case EnvShows::kFree: return shape && !synced;
      case EnvShows::kSynced: return shape && synced;
      case EnvShows::kChained: return shape && chainedTo(part, e.env) >= 0;
      }
      return true;
   }
   bool shapeMode(int part, int e) const { return static_cast<int>(value(part, envParam(e, kEnvMode)) + 0.5) == kModeShape; }
   // As the engine's: the envelope `e` waits for in its chain, when both are in Shape mode.
   int chainedTo(int part, int e) const {
      const int before = chainedAfter(static_cast<int>(value(part, chainParam(kChain)) + 0.5), e);
      return before >= 0 && shapeMode(part, e) && shapeMode(part, before) ? before : -1;
   }
   // Everything envShows() reads, so a change from the host shows.
   double envModeSum() const {
      double sum = 0.0;
      for (int part = 0; part < kPartCount; ++part) {
         sum += value(part, chainParam(kChain)) * (part + 1) * 1000.0;
         for (int e = 0; e < kEnvCount; ++e)
            sum += (value(part, envParam(e, kEnvMode)) * 3.0 + value(part, shapeParam(e, kShapeSync))) * (part * 3 + e + 1) * 10.0;
      }
      return sum;
   }

   // Shows each oscillator's pitch knob for its pitch mode.
   void refreshPitch() {
      for (const PitchKnobs &p : pitchKnobs_) {
         const int mode = static_cast<int>(value(p.part, oscParam(p.osc, kOscPitchMode)) + 0.5);
         const bool shown = shows(p.part, p.group);
         p.coarse->setVisible(shown && mode == 0);
         p.ratio->setVisible(shown && mode == 1);
         p.fixed->setVisible(shown && mode == 2);
      }
   }

   // Spreads each envelope editor's times by its Time Range. The times are already on a log taper, so
   // the editor places them by milliseconds (on a cube) rather than drawing the taper bent again.
   void refreshEnvRanges() {
      for (int part = 0; part < kPartCount; ++part)
         for (int e = 0; e < kEnvCount; ++e) {
            ui::EnvelopeEditor *env = envelopes_[part][e];
            const int range = static_cast<int>(value(part, envParam(e, kEnvRange)) + 0.5);
            if (!env || range == envRanges_[part][e])
               continue;
            envRanges_[part][e] = range;
            env->setTimeMapping([range](double v) { return envPosition(v, range); },
                                [range](float position) { return envPositionValue(position, range); });
         }
   }

   // Adds a widget for the part and group being built.
   template <typename T, typename... Args>
   T &put(ui::Rect bounds, Args &&...args) {
      T &frame = add<T>(bounds, std::forward<Args>(args)...);
      placed_.push_back({&frame, part_, group_, envShows_});
      return frame;
   }
   template <typename T>
   T &glassy(T &widget, unsigned accent) {
      widget.setTheme(glassTheme());
      widget.setAccent(accent);
      return widget;
   }
   Glass &glass(ui::Rect r, const char *title, unsigned accent) {
      auto &g = put<Glass>(r, title, accent);
      g.designHeight = r.h;
      return g;
   }
   ui::Label &label(ui::Rect r, const std::string &t, unsigned color = kText,
                    visage::Font::Justification j = visage::Font::kCenter) {
      return put<ui::Label>(r, t, color, j);
   }
   clap_id idOf(clap_id offset) const { return part_ < 0 ? offset : partId(part_, offset); }

   // A knob for a param: a part param (as an offset) while building a part, which MAP can map, or
   // a global one.
   ui::Knob &knob(clap_id offset, const char *name, unsigned accent, ui::Rect cell, const char *tip = nullptr) {
      const clap_id id = idOf(offset);
      ui::Knob *k;
      if (part_ >= 0) {
         auto &m = put<MapKnob>(cell, rangeOf(id), *this, part_, offset);
         mapKnobs_.push_back(&m);
         k = &m;
      }
      else
         k = &put<ui::Knob>(cell, rangeOf(id));
      glassy(*k, accent);
      k->setLabel(name);
      k->valueText = [id](double v) { return text(id, v); };
      k->parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(*k, id);
      if (tip)
         tips_.add(*k, tip);
      return *k;
   }
   ui::Dropdown &dropdown(clap_id offset, unsigned accent, ui::Rect r) {
      const clap_id id = idOf(offset);
      auto &d = glassy(put<ui::Dropdown>(r, upper(*findParam(id)->choices)), accent);
      bindings_.bind(d, id);
      return d;
   }
   // A value in a box, `prefix` before it, for a param with no room for a knob.
   ui::NumberBox &numberBox(clap_id offset, const std::string &prefix, unsigned accent, ui::Rect r, const char *tip) {
      const clap_id id = idOf(offset);
      ui::NumberBox *made;
      if (part_ >= 0) {
         auto &m = put<MapBox>(r, rangeOf(id), *this, part_, offset);
         mapped_.push_back(&m);
         made = &m;
      }
      else
         made = &put<ui::NumberBox>(r, rangeOf(id));
      auto &box = glassy(*made, accent);
      box.valueText = [id, prefix](double v) { return prefix + text(id, v); };
      box.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(box, id);
      tips_.add(box, tip);
      return box;
   }
   static std::vector<std::string> shapeOptions() {
      std::vector<std::string> names;
      for (const ShapeBank &b : shapeBanks())
         for (const char *n : b.shapes)
            names.push_back(upper(n));
      return names;
   }
   static std::vector<std::pair<std::string, int>> shapeGroups() {
      std::vector<std::pair<std::string, int>> groups;
      for (const ShapeBank &b : shapeBanks())
         groups.push_back({upper(b.name), kShapeBankSize});
      return groups;
   }
   static std::vector<std::string> chainOptions() {
      std::vector<std::string> names;
      for (const std::string &n : kChainNames)
         names.push_back(n == "Off" ? "NO CHAIN" : "CHAIN " + upper(n));
      return names;
   }
   ui::SegmentedSwitch &segments(clap_id offset, unsigned accent, ui::Rect r, std::vector<std::string> names) {
      const clap_id id = idOf(offset);
      auto &sw = glassy(put<ui::SegmentedSwitch>(r, std::move(names)), accent);
      bindings_.bind(sw, id);
      return sw;
   }
   // A source to map: touching one turns MAP on with it armed, and touching the armed one again
   // lets it go.
   SourceChip &chip(int source, const std::string &name, ui::Rect r, const char *tip = nullptr) {
      auto &c = put<SourceChip>(r, source, name);
      c.onClick = [this](int s) { setMapping(true, mapping_ && armed_ == s ? kSourceOff : s); };
      chips_.push_back(&c);
      if (tip)
         tips_.add(c, tip);
      return c;
   }

   void buildHeader() {
      update_ = &glassy(add<ui::UpdateNotice>({146, 13, 220, 26}), kPartColors[0]);
      put<PresetBar>({416, 13, 250, 26}, host_);
      auto &parts = glassy(put<ui::SegmentedSwitch>({676, 13, 146, 26}, std::vector<std::string>{"PART A", "PART B"}),
                           kPartColors[0]);
      parts.onChange = [this](int p) { showPart(p); };
      partSwitch_ = &parts;
      tips_.add(parts, "Each part is a species with its own patch and its own way of flying. The panels edit "
                       "this one; the sky shows both.");
      auto &map = glassy(put<ui::Button>({954, 13, 62, 26}, "MAP"), kMapColor);
      map.setToggle(true);
      map.onToggle = [this](bool on) { setMapping(on, kSourceOff); };
      mapButton_ = &map;
      tips_.add(map, "Touch a source (which turns MAP on), then drag any knob to set how much the source moves it. "
                     "Double-click a knob to flip its route, right-click to remove it.");
      const auto cells = ui::row({1026, 13, 398, 26}, kTouchSourceCount, 4);
      for (int i = 0; i < kTouchSourceCount; ++i)
         chip(kSourceVelocity + i, upper(kTouchNames[i]), cells[i]);
      auto &mode = glassy(put<ui::Dropdown>({832, 13, 112, 26}, upper(kPlayModeNames)), kPartColors[0]);
      bindings_.bind(mode, kPlayMode);
      tips_.add(mode, "Which species a note hatches: A only, both, split by key or by velocity, by MPE zone, "
                      "or crossfaded by Slide or Press.");
      // RINGS and ROUTES sit under the sky, after the flock's own sources.
      group_ = kSynth;
      auto &rings = glassy(put<ui::Button>({kSkyX + kSkyW - 162, kSkyY + kSkyH + 12, 70, 24}, "RINGS"), kDim);
      rings.setToggle(true);
      rings.setOn(host_.ringsShown);
      view_->showRings = host_.ringsShown;
      rings.onToggle = [this](bool on) { view_->showRings = host_.ringsShown = on; };
      tips_.add(rings, "Show each held note's lure: the ring its creature is pulled toward, placed by LURE X "
                       "and LURE Y on the FLOCK tab.");
      auto &routes = glassy(put<ui::Button>({kSkyX + kSkyW - 84, kSkyY + kSkyH + 12, 84, 24}, "ROUTES"), kMapColor);
      routes.setToggle(true);
      routes.onToggle = [this](bool on) { showRoutes(on); };
      routesButton_ = &routes;
      tips_.add(routes, "Every route of this part as a list, to edit by name.");
      group_ = kAlways;
      // Not put(): it stays when the panels hide.
      auto &views = glassy(add<ui::SegmentedSwitch>({1496, 13, 136, 26}, std::vector<std::string>{"SYNTH", "SKY"}), kDim);
      views.onChange = [this](int v) { setView(v); };
      viewSwitch_ = &views;
      tips_.add(views, "SYNTH shows the sound's panels beside the sky. SKY hides everything but the flocks.");
   }

   // One part's panels: the oscillators, the filters and the envelopes down the left, each drawn as
   // well as set, and its voice, flock and motion under the sky.
   void buildPart(int part) {
      const unsigned c = kPartColors[part];
      const float x = 16, w = 880;
      // Oscillators, on two tabs: OSCILLATORS has a row each, its wave drawn beside its knobs; FM the
      // grid, drawn as who modulates whom, and the links between creatures.
      glass({x, 64, w, 330}, "", c);
      group_ = kOscPage;
      for (int o = 0; o < kOscCount; ++o) {
         const unsigned oc = kOscColors[o];
         const float y = 98 + o * 98.0f;
         label({x + 14, y + 6, 120, 20}, "OSC " + std::to_string(o + 1), oc, visage::Font::kLeft);
         // Where each note starts the wave: a fresh random place, or always at its Phase.
         segments(oscParam(o, kOscRetrigger), oc, {x + 62, y + 4, 72, 20}, std::vector<std::string>{"RAND", "SYNC"});
         dropdown(oscParam(o, kOscWave), oc, {x + 14, y + 32, 120, 26});
         segments(oscParam(o, kOscPitchMode), oc, {x + 14, y + 62, 120, 24}, std::vector<std::string>{"NOTE", "RATIO", "HZ"});
         const auto k = ui::row({x + 436, y - 4, 370, 104}, 5);
         auto &coarse = knob(oscParam(o, kOscCoarse), "PITCH", oc, k[0]);
         auto &ratio = knob(oscParam(o, kOscRatio), "RATIO", oc, k[0]);
         auto &fixed = knob(oscParam(o, kOscFixed), "FREQ", oc, k[0]);
         pitchKnobs_.push_back({part, o, &coarse, &ratio, &fixed, kOscPage});
         knob(oscParam(o, kOscFine), "FINE", oc, k[1]);
         knob(oscParam(o, kOscShape), "SHAPE", oc, k[2]);
         knob(mixParam(kMixLevel, o), "LEVEL", oc, k[3]);
         knob(mixParam(kMixPan, o), "PAN", oc, k[4]);
         label({x + 808, y + 10, 64, 16}, "FILTER");
         dropdown(oscParam(o, kOscFilter), oc, {x + 810, y + 36, 60, 26});
      }
      group_ = kFmPage;
      label({x + 340, 98, 300, 16}, "ROW INTO COLUMN", kDim);
      const auto g = ui::grid({x + 330, 112, 320, 280}, 3, 3);
      static const char *names[3][3] = {{"FB 1", "1 > 2", "1 > 3"}, {"2 > 1", "FB 2", "2 > 3"}, {"3 > 1", "3 > 2", "FB 3"}};
      for (int from = 0; from < kOscCount; ++from)
         for (int to = 0; to < kOscCount; ++to)
            knob(fmCell(from, to), names[from][to], kOscColors[from], g[from * 3 + to]);
      segments(kFmBase + kFmType, c, {x + 670, 98, 196, 24}, std::vector<std::string>{"PHASE", "EXP"});
      label({x + 670, 132, 196, 16}, "BETWEEN CREATURES", kDim);
      const auto b = ui::grid({x + 664, 150, 208, 240}, 3, 2);
      knob(flockParam(kNeighbourFm), "FLOCK FM", c, b[0], "The nearest neighbours' Osc 1 into this creature's Osc 2.");
      knob(flockParam(kTunePull), "TUNE PULL", c, b[1], "Creatures flying the same way drift toward pure intervals.");
      knob(flockParam(kBite), "BITE", c, b[2], "Ring mod while a hunter holds this creature.");
      knob(mixParam(kNoiseLevel), "NOISE", c, b[3]);
      knob(mixParam(kNoiseColor), "COLOR", c, b[4]);
      knob(kDrift, "DRIFT", c, b[5], "Each creature's own slow wander in pitch.");
      group_ = kSynth;

      // Filters: their response drawn across the panel, then each one's knobs under its half.
      glass({x, 404, w, 313}, "FILTER", c);
      segments(tailParam(kFilterRouting), c, {x + 90, 410, 160, 24}, std::vector<std::string>{"SERIAL", "PARALLEL"});
      label({x + 270, 414, 90, 16}, "NOISE INTO", kDim, visage::Font::kRight);
      tips_.add(dropdown(mixParam(kNoiseFilter), c, {x + 366, 410, 66, 24}), "Which filter the noise goes into.");
      const auto k = ui::row({x + 8, 605, w - 16, 104}, 10);
      for (int f = 0; f < kFilterCount; ++f) {
         const unsigned fc = f == 0 ? c : sourceColor(kSourceLfo1 + 1);
         const ui::Rect &first = k[f * 4];
         // Its name first, then its type and slope.
         label({first.x, 604, first.w, 18}, f == 0 ? "FILTER 1" : "FILTER 2", f == 0 ? 0xffffffff : 0xff9fd8ff);
         dropdown(filterParam(f, kFilterType), c, {k[f * 4 + 1].x + 4, 601, 2 * first.w - 8, 24});
         dropdown(filterParam(f, kFilterSlope), c, {k[f * 4 + 3].x + 4, 601, first.w - 8, 24});
         knob(filterParam(f, kFilterCutoff), "CUTOFF", fc, {k[f * 4].x, 629, k[f * 4].w, 96});
         knob(filterParam(f, kFilterResonance), "RES", fc, {k[f * 4 + 1].x, 629, k[f * 4 + 1].w, 96});
         knob(filterParam(f, kFilterDrive), "DRIVE", fc, {k[f * 4 + 2].x, 629, k[f * 4 + 2].w, 96});
         knob(filterParam(f, kFilterKeyTrack), "KEY", fc, {k[f * 4 + 3].x, 629, k[f * 4 + 3].w, 96});
      }
      knob(tailParam(kFilterBalance), "BALANCE", c, {k[8].x, 629, k[8].w, 96}, "In parallel, turns Filter 1 or Filter 2 down.");
      // After both filters: the drive, by its kind.
      dropdown(tailParam(kDriveType), c, {k[9].x + 4, 601, k[9].w - 8, 24});
      knob(tailParam(kDriveAmount), "PRE DRIVE", c, {k[9].x, 629, k[9].w, 96}, "Drive before both filters, of the kind above.");

      // Envelopes: three big editors side by side.
      // Envelopes and LFOs, on two tabs: three big envelope editors, or the two LFOs drawn.
      glass({x, 727, w, 197}, "", c);
      group_ = kEnvPage;
      const char *envNames[kEnvCount] = {"ENV 1  AMP", "ENV 2", "ENV 3"};
      for (int e = 0; e < kEnvCount; ++e) {
         const float ex = x + 12 + e * 288.0f;
         const unsigned ec = sourceColor(kSourceEnv1 + e);
         chip(kSourceEnv1 + e, envNames[e], {ex, 763, 88, 24});
         auto &mode = dropdown(envParam(e, kEnvMode), c, {ex + 92, 763, 58, 24});
         tips_.add(mode, "ADSR draws the envelope from its stages. SHAPE plays a contour from the shape library "
                         "instead, stretched over its time, warped, bent and repeated, and can chain into the next.");
         const auto id = [&](EnvParam p) { return partId(part, envParam(e, p)); };
         const auto sid = [&](ShapeParam p) { return partId(part, shapeParam(e, p)); };

         // ADSR: the editor and its view and loop.
         envShows_ = {e, EnvShows::kAdsr};
         auto &range = dropdown(envParam(e, kEnvRange), c, {ex + 154, 763, 66, 24});
         tips_.add(range, "How much time the editor spreads across its width: up to 1 s, 5 s or 20 s a stage. "
                          "Switching it changes no time. Hold shift while dragging a corner for fine moves.");
         dropdown(envParam(e, kEnvLoop), c, {ex + 224, 763, 52, 24});
         auto &env = glassy(put<ui::EnvelopeEditor>({ex, 793, 276, 127}, rangeOf(id(kEnvAttack)), rangeOf(id(kEnvDecay)),
                                                    rangeOf(id(kEnvSustain)), rangeOf(id(kEnvRelease))),
                            ec);
         envelopes_[part][e] = &env;
         envRanges_[part][e] = -1;
         bindings_.bind(env, id(kEnvAttack), id(kEnvDecay), id(kEnvSustain), id(kEnvRelease));

         // Shape: which contour, drawn, and how it plays.
         envShows_ = {e, EnvShows::kShape};
         auto &shape = glassy(put<ui::Dropdown>({ex + 154, 763, 122, 24}, shapeOptions()), ec);
         shape.setGroups(shapeGroups());
         bindings_.bind(shape, sid(kShapeIndex));
         tips_.add(shape, "The contour this envelope plays, from eight banks of six.");
         auto &view = glassy(put<ShapeView>({ex, 793, 276, 66}, *view_, *this, part, e, rangeOf(sid(kShapeWarp)),
                                            rangeOf(sid(kShapeBend))),
                             ec);
         bindings_.bind(view, sid(kShapeWarp), sid(kShapeBend));
         view.x().valueText = [i = sid(kShapeWarp)](double v) { return text(i, v); };
         view.y().valueText = [i = sid(kShapeBend)](double v) { return text(i, v); };
         tips_.add(view, "Drag sideways to warp the shape in time and up or down to bend it in level. Hold shift for "
                         "fine moves; double-click to put both back. In MAP, drag to set how much the source warps "
                         "or bends it.");
         shapes_[part][e] = &view;
         mapped_.push_back(&view);
         drawings_.push_back(&view);
         envShows_ = {e, EnvShows::kFree};
         numberBox(shapeParam(e, kShapeTime), "TIME ", ec, {ex, 864, 104, 24},
                   "How long one pass of the shape takes: 1 ms to 30 minutes.");
         envShows_ = {e, EnvShows::kSynced};
         auto &length = dropdown(shapeParam(e, kShapeDivision), ec, {ex, 864, 104, 24});
         tips_.add(length, "How long one pass of the shape takes, in the host's tempo.");
         envShows_ = {e, EnvShows::kShape};
         segments(shapeParam(e, kShapeSync), ec, {ex + 108, 864, 70, 24}, std::vector<std::string>{"FREE", "SYNC"});
         auto &repeat = dropdown(shapeParam(e, kShapeRepeat), ec, {ex + 182, 864, 94, 24});
         tips_.add(repeat, "How many passes it plays, then holds where it ends. HELD repeats while the note is held "
                           "and finishes its pass after; FOREVER until the voice ends. In a chain both play once.");
         if (e == 0)
            numberBox(envParam(e, kEnvRelease), "REL ", ec, {ex, 892, 104, 24},
                      "The amp's fade after the note ends, while the shape plays on under it.");
         else
            numberBox(envParam(e, kEnvVelocity), "VEL ", ec, {ex, 892, 104, 24},
                      "How much the note's velocity scales the envelope.");
         auto &reverse = glassy(put<ui::Button>({ex + 108, 892, 40, 24}, "REV"), ec);
         reverse.setToggle(true);
         bindings_.bind(reverse, sid(kShapeReverse));
         tips_.add(reverse, "Plays the shape backwards.");
         auto &retrigger = dropdown(shapeParam(e, kShapeRetrigger), ec, {ex + 152, 892, 70, 24});
         tips_.add(retrigger, "What a new note does while it plays: SMOOTH picks the shape up where it is at the level "
                              "it is now, RESTART starts it from the top, IGNORE lets it play on.");
         envShows_ = {e, EnvShows::kChained};
         numberBox(shapeParam(e, kShapePhase), "", ec, {ex + 226, 892, 50, 24},
                   "When it starts in the chain: how far through the envelope before it, from with it (0%) to "
                   "after it ends (100%).");
         envShows_ = {};
      }
      auto &chain = glassy(put<ui::Dropdown>({x + w - 12 - 150, 731, 150, 24}, chainOptions()), c);
      bindings_.bind(chain, partId(part, chainParam(kChain)));
      tips_.add(chain, "Chains envelopes in Shape mode: each starts the next as it plays, at the next one's phase "
                       "(the box beside its REV), and a loop goes back round to the first.");
      group_ = kLfoPage;
      for (int i = 0; i < kLfoCount - kMotionCount; ++i) {
         const int l = kMotionCount + i;
         const float lx = x + 12 + i * 436.0f;
         const unsigned lc = sourceColor(kSourceLfo1 + i);
         chip(kSourceLfo1 + i, "LFO " + std::to_string(i + 1), {lx, 763, 104, 24});
         dropdown(lfoParam(l, kLfoShape), lc, {lx + 112, 763, 96, 24});
         dropdown(lfoParam(l, kLfoTrigger), c, {lx + 216, 763, 100, 24});
         segments(lfoParam(l, kLfoSync), lc, {lx + 324, 763, 96, 24}, std::vector<std::string>{"FREE", "SYNC"});
         const auto k = ui::row({lx + 208, 793, 212, 94}, 3);
         knob(lfoParam(l, kLfoRate), "RATE", lc, k[0]);
         knob(lfoParam(l, kLfoPhase), "PHASE", lc, k[1]);
         knob(lfoParam(l, kLfoDelay), "DELAY", lc, k[2]);
         dropdown(lfoParam(l, kLfoDivision), lc, {lx + 212, 892, 100, 24});
         dropdown(lfoParam(l, kLfoPolarity), lc, {lx + 320, 892, 100, 24});
      }
      group_ = kSynth;

      // Under the sky: the part's voice and the output, big. The drawings are shared (see
      // buildShared()); the knobs sit under them.
      // On four tabs: VOICE, FLOCK (how it flies), MOTION (the wingbeat and the wind) and TOUCH.
      const ui::Rect vb = kVoiceBox;
      glass(vb, "", c);
      label({vb.x + vb.w - 96, vb.y + 12, 84, 18}, part == 0 ? "PART A" : "PART B", c, visage::Font::kRight);
      group_ = kVoicePage;
      segments(kVoiceMode, c, {vb.x + 340, vb.y + 8, 180, 26}, std::vector<std::string>{"POLY", "MONO", "LEGATO"});
      auto &voices = glassy(put<ui::NumberBox>({vb.x + 528, vb.y + 8, 76, 26}, rangeOf(partId(part, kPolyphony), 1.0)), c);
      voices.setStepButtons(true);
      voices.valueText = [](double v) { return std::to_string(static_cast<int>(v + 0.5)); };
      bindings_.bind(voices, partId(part, kPolyphony));
      tips_.add(voices, "How many creatures this species can have at once: one per held note, up to 16.");
      // One row of knobs, each under its drawing: the stereo field, the voices, the levels.
      const auto vk = kVoiceKnobs();
      knob(kPartPan, "PAN", c, vk[0]);
      knob(kSkyPan, "X PAN", c, vk[1], "How far a creature's place in the sky, left to right, pans its voice.");
      knob(kGlideTime, "GLIDE", c, vk[3]);
      knob(kBendRange, "BEND", c, vk[4], "The MPE bend range: how far a finger slides, in semitones.");
      knob(kPartTranspose, "TRANSP", c, vk[5]);
      knob(kVelocityToAmp, "VELOCITY", c, vk[6], "How much velocity sets the level.");
      knob(kPartLevel, "LEVEL", c, vk[7]);
      shared(part, [&] {
         knob(kOutputWidth, "WIDTH", kDim, vk[2], "The whole output's stereo width: 0% is mono, past 100% wider.");
         knob(kMasterLevel, "MASTER", kDim, vk[8]);
      });

      group_ = kFlockPage;
      // Left, what it is and where the lures sit; right, how it flies.
      const float fx = vb.x + 14, fw = 150;
      const auto pick = [&](float y, const char *name, clap_id id, unsigned col) -> ui::Dropdown & {
         label({fx, y, fw, 16}, name, kDim, visage::Font::kLeft);
         return dropdown(id, col, {fx, y + 18, fw, 26});
      };
      tips_.add(pick(vb.y + 44, "BEHAVIOUR", flockParam(kBehaviour), c),
                "SHOAL flies with its own kind, SCATTER keeps apart, HUNT chases the other species and FLEE runs from it.");
      tips_.add(pick(vb.y + 100, "LURE X", flockParam(kLureX), c), "What places a note's ring left to right.");
      tips_.add(pick(vb.y + 156, "LURE Y", flockParam(kLureY), c), "What places a note's ring bottom to top.");
      shared(part, [&] {
         tips_.add(pick(vb.y + 212, "WALLS", kSkyWalls, kDim),
                   "What the sky's edges do: turn creatures back, wrap them round, or let them roam.");
      });
      const auto fk = ui::grid({vb.x + 176, vb.y + 40, vb.w - 186, 232}, 5, 2);
      knob(flockParam(kCohesion), "COHESION", c, fk[0], "Pull toward the middle of the neighbours.");
      knob(flockParam(kAlignment), "ALIGN", c, fk[1], "Turn to fly the way the neighbours fly.");
      knob(flockParam(kSeparation), "SEPARATE", c, fk[2], "Keep clear of the nearest.");
      knob(flockParam(kSpeed), "SPEED", c, fk[3]);
      knob(flockParam(kWander), "WANDER", c, fk[4], "Each creature's own restlessness.");
      knob(flockParam(kLure), "LURE", c, fk[5], "How hard your finger pulls its creature.");
      knob(flockParam(kRange), "RANGE", c, fk[6], "How near counts as a neighbour.");
      knob(flockParam(kTailLength), "TAIL", c, fk[7], "How long the ink lasts behind a creature.");
      knob(flockParam(kTailSpread), "SPREAD", c, fk[8], "How far the ink blooms as it ages.");
      shared(part, [&] {
         knob(kSkySize, "SIZE", kDim, fk[9], "How big the sky is, so how far apart the flocks can spread.");
      });

      group_ = kMotionPage;
      for (int l = 0; l < kMotionCount; ++l) {
         const float mx = vb.x + 10 + l * 356.0f, my = vb.y + 44;
         const unsigned lc = kMotionColors[l];
         tips_.add(label({mx, my + 4, 88, 16}, l == 0 ? "WINGBEAT" : "WIND", lc, visage::Font::kLeft),
                   l == 0 ? "One per creature: each bobs across its path." : "One gust for the whole sky, pushing every creature.");
         dropdown(lfoParam(l, kLfoShape), lc, {mx + 88, my, 80, 24});
         dropdown(lfoParam(l, kLfoTrigger), c, {mx + 174, my, 92, 24});
         segments(lfoParam(l, kLfoSync), lc, {mx + 272, my, 72, 24}, std::vector<std::string>{"FREE", "SYNC"});
         const auto mk = ui::row({mx - 4, my + 34, 352, 110}, 4);
         knob(lfoParam(l, kLfoRate), "RATE", lc, mk[0]);
         knob(lfoParam(l, kLfoPush), "PUSH", lc, mk[1], l == 0 ? "How far each creature bobs." : "How hard the gust pushes.");
         knob(lfoParam(l, kLfoPhase), "PHASE", lc, mk[2]);
         knob(lfoParam(l, kLfoDelay), "DELAY", lc, mk[3]);
         label({mx, my + 152, 168, 16}, "DIVISION", kDim, visage::Font::kLeft);
         dropdown(lfoParam(l, kLfoDivision), lc, {mx, my + 170, 168, 26});
         label({mx + 176, my + 152, 168, 16}, "POLARITY", kDim, visage::Font::kLeft);
         dropdown(lfoParam(l, kLfoPolarity), lc, {mx + 176, my + 170, 168, 26});
      }
      group_ = kSynth;
   }

   // Builds controls of params that aren't the part's own in `build`, then hands them to `part`, so
   // they show and hide with its page.
   template <typename Build> void shared(int part, Build build) {
      const int was = part_;
      const size_t from = placed_.size();
      part_ = -1;
      build();
      part_ = was;
      for (size_t i = from; i < placed_.size(); ++i)
         placed_[i].part = part;
   }

   // The panels shared by both parts: the tabs, the drawings (which follow the shown part), the
   // flock's sources under the sky, and the touch settings.
   void buildShared() {
      auto &tabs = glassy(put<ui::TabBar>({26, 68, 260, 26}, std::vector<std::string>{"OSCILLATORS", "FM"}), kDim);
      tabs.select(oscTab_);
      tabs.onChange = [this](int t) {
         oscTab_ = t;
         refresh();
      };
      group_ = kOscPage;
      for (int o = 0; o < kOscCount; ++o)
         drawings_.push_back(&put<WaveView>({164, 98 + o * 98.0f, 276, 92}, host_, *view_, o, kOscColors[o]));
      group_ = kFmPage;
      drawings_.push_back(&put<FmView>({28, 100, 320, 290}, host_, *view_));
      group_ = kSynth;
      auto &envTabs = glassy(put<ui::TabBar>({26, 731, 260, 26}, std::vector<std::string>{"ENVELOPES", "LFOS"}), kDim);
      envTabs.select(envTab_);
      envTabs.onChange = [this](int t) {
         envTab_ = t;
         refresh();
      };
      group_ = kLfoPage;
      for (int i = 0; i < kLfoCount - kMotionCount; ++i)
         drawings_.push_back(&put<LfoView>({28 + i * 436.0f, 793, 196, 123}, host_, *view_, kMotionCount + i,
                                           sourceColor(kSourceLfo1 + i), time_));
      group_ = kSynth;
      drawings_.push_back(&put<FilterView>({28, 440, 856, 157}, host_, *this, *view_));
      const ui::Rect vb = kVoiceBox;
      // Each drawing spans the knobs under it.
      const auto k = kVoiceKnobs();
      const auto over = [&](int from, int to) {
         return ui::Rect{k[from].x + 6, vb.y + 44, k[to].x + k[to].w - k[from].x - 12, 124};
      };
      voiceTabs_ = &glassy(
          put<ui::TabBar>({vb.x + 8, vb.y + 6, 324, 26}, std::vector<std::string>{"VOICE", "FLOCK", "MOTION", "TOUCH"}), kDim);
      voiceTabs_->select(voiceTab_);
      voiceTabs_->onChange = [this](int t) {
         voiceTab_ = t;
         refresh();
      };
      group_ = kVoicePage;
      drawings_.push_back(&put<StereoView>(over(0, 2), host_, *view_));
      drawings_.push_back(&put<VoiceSlotsView>(over(3, 6), host_, *view_));
      drawings_.push_back(&put<LevelView>(over(7, 8), host_, meters_));

      // How each touch responds, and pitch rounding: shared by both parts.
      group_ = kTouchPage;
      const auto tk = ui::row({vb.x + 8, vb.y + 64, vb.w - 16, 100}, 2 * kTouchCount);
      for (int i = 0; i < kTouchCount; ++i) {
         const unsigned tc = touchColor(i);
         label({tk[2 * i].x, vb.y + 44, 2 * tk[2 * i].w, 16}, upper(kTouchNames[i]), tc);
         knob(kTouchCurve1 + i, "CURVE", tc, tk[2 * i]);
         knob(kTouchSmooth1 + i, "SMOOTH", tc, tk[2 * i + 1]);
      }
      const auto pk = ui::row({vb.x + 8, vb.y + 172, vb.w - 16, 100}, 2 * kTouchCount);
      knob(kGlideRounding, "ROUND", kDim, pk[0], "Pulls a resting finger to the nearest semitone.");
      knob(kRoundingTime, "ROUND TIME", kDim, pk[1]);
      knob(kManagerBendRange, "MGR BEND", kDim, pk[2], "The MPE manager channel's bend range.");
      group_ = kSynth;
      const std::pair<int, const char *> sources[] = {
          {kSourceSpeed, "SPEED"}, {kSourceNearest, "NEAREST"}, {kSourceHeading, "HEADING"}, {kSourceTurn, "TURN"},
          {kSourceRandom, "RANDOM"}};
      for (int i = 0; i < 5; ++i)
         chip(sources[i].first, sources[i].second, {kSkyX + 8 + i * 84.0f, kSkyY + kSkyH + 12, 80, 24});
      group_ = kAlways;
   }

   // The route list: every route of a part, over the sky.
   // The route list over the sky: a row for each route the part uses, in route order, sixteen to a
   // page. Every route of the pool has its row built up front; layoutRoutes() places the used ones.
   void buildRoutes(int part) {
      group_ = kRoutes;
      const unsigned c = kPartColors[part];
      glass(kRoutesBox, part == 0 ? "ROUTES  PART A" : "ROUTES  PART B", kMapColor);
      const std::vector<std::string> sources = upper(kSourceNames);
      const std::vector<std::string> &dests = destinationNames();
      for (int r = 0; r < kRouteCount; ++r) {
         RouteRow row{part, r, {}};
         auto &number = put<ui::Label>({0, 0, 22, 16}, std::to_string(r + 1), kDim, visage::Font::kLeft);
         auto &src = glassy(put<ui::Dropdown>({0, 0, 104, 22}, sources), kMapColor);
         bindings_.bind(src, partId(part, routeParam(r, kRouteSource)));
         auto &dst = glassy(put<ui::Dropdown>({0, 0, 166, 22}, dests), c);
         bindings_.bind(dst, partId(part, routeParam(r, kRouteDestination)));
         auto &remove = glassy(put<ui::Button>({0, 0, 22, 22}, "X"), kMapColor);
         remove.onClick = [this, part, r] {
            set(part, routeParam(r, kRouteSource), kSourceOff);
            set(part, routeParam(r, kRouteAmount), 0.0);
            layoutRoutes();
         };
         tips_.add(remove, "Remove this route.");
         const clap_id amount = partId(part, routeParam(r, kRouteAmount));
         auto &amt = glassy(put<ui::Slider>({0, 0, 298, 20}, ui::Slider::Orientation::Horizontal, rangeOf(amount)),
                            kMapColor);
         amt.valueText = [amount](double v) { return text(amount, v); };
         bindings_.bind(amt, amount);
         row.frames = {&number, &src, &dst, &remove, &amt};
         routeRows_.push_back(row);
      }
      group_ = kAlways;
   }

   // The list's own controls, shared by both parts: adding a route, the pages, and the count.
   void buildRouteControls() {
      group_ = kRoutes;
      const ui::Rect b = kRoutesBox;
      routeInfo_ = &put<ui::Label>({b.x + 178, b.y + 7, 360, 16}, "", kDim, visage::Font::kRight);
      auto &add = glassy(put<ui::Button>({b.x + 548, b.y + 4, 70, 22}, "+ ADD"), kMapColor);
      add.onClick = [this] {
         // A new route from Env 2 to Filter 1's cutoff, at zero: pick its source and destination here.
         for (int r = 0; r < kRouteCount; ++r)
            if (static_cast<int>(value(host_.part, routeParam(r, kRouteSource)) + 0.5) == kSourceOff) {
               set(host_.part, routeParam(r, kRouteSource), kSourceEnv1 + 1);
               set(host_.part, routeParam(r, kRouteDestination), destinationIndex(filterParam(0, kFilterCutoff)));
               set(host_.part, routeParam(r, kRouteAmount), 0.0);
               break;
            }
         routePage_ = 99; // the last page, where the new route is
         layoutRoutes();
      };
      tips_.add(add, "Add a route, then pick its source and destination. Or touch a source and drag any "
                     "knob.");
      auto &prev = glassy(put<ui::Button>({b.x + 624, b.y + 4, 30, 22}, "<"), kMapColor);
      prev.onClick = [this] {
         routePage_ = std::max(0, routePage_ - 1);
         layoutRoutes();
      };
      auto &next = glassy(put<ui::Button>({b.x + 658, b.y + 4, 30, 22}, ">"), kMapColor);
      next.onClick = [this] {
         ++routePage_;
         layoutRoutes();
      };
      routeEmpty_ = &put<ui::Label>({b.x + 20, b.y + 214, 656, 20}, "No routes yet. Press + ADD, or touch a source and drag any "
                                                          "knob.", kDim);
      group_ = kAlways;
   }

   // Places the shown part's routes in use on the list's current page, and hides every other row.
   void layoutRoutes() {
      std::vector<const RouteRow *> used;
      for (const RouteRow &row : routeRows_)
         if (row.part == host_.part && static_cast<int>(value(row.part, routeParam(row.route, kRouteSource)) + 0.5) != kSourceOff)
            used.push_back(&row);
      const int pages = std::max(1, (static_cast<int>(used.size()) + 15) / 16);
      routePage_ = std::clamp(routePage_, 0, pages - 1);
      for (const RouteRow &row : routeRows_)
         for (visage::Frame *f : row.frames)
            f->setVisible(false);
      const bool open = host_.routesOpen;
      for (int i = routePage_ * 16; open && i < std::min<int>(static_cast<int>(used.size()), (routePage_ + 1) * 16); ++i) {
         const int slot = i - routePage_ * 16;
         const float x = kRoutesBox.x + 14 + (slot / 8) * 340.0f, y = kRoutesBox.y + 36 + (slot % 8) * 56.0f;
         const RouteRow &row = *used[i];
         const ui::Rect rects[] = {{x, y + 3, 22, 16}, {x + 22, y, 104, 22}, {x + 130, y, 164, 22}, {x + 298, y, 22, 22},
                                   {x + 22, y + 26, 298, 20}};
         for (int k = 0; k < 5; ++k) {
            move(*row.frames[k], rects[k]);
            row.frames[k]->setVisible(true);
         }
      }
      char info[64];
      std::snprintf(info, sizeof(info), "%d of %d routes   page %d of %d", static_cast<int>(used.size()), kRouteCount,
                    routePage_ + 1, pages);
      routeInfo_->setText(info);
      routeEmpty_->setVisible(open && used.empty());
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::UpdateNotice *update_ = nullptr;
   FlockView *view_ = nullptr;
   ui::SegmentedSwitch *partSwitch_ = nullptr;
   ui::Button *mapButton_ = nullptr;
   ui::Button *routesButton_ = nullptr;
   static constexpr int kSynthView = 0, kSkyOnly = 1;
   static constexpr ui::Rect kVoiceBox{kSkyX, 650, kSkyW, 274};
   // The route list, over the sky.
   static constexpr ui::Rect kRoutesBox{kSkyX + 12, 80, kSkyW - 24, 494};
   // The voice panel's knobs, one row along its bottom.
   static std::vector<ui::Rect> kVoiceKnobs() { return ui::row({kVoiceBox.x + 8, kVoiceBox.y + 172, kVoiceBox.w - 16, 100}, 9); }
   static constexpr float kCleanMargin = 16.0f;
   std::vector<Placed> placed_;
   std::vector<MapKnob *> mapKnobs_;
   // The other things MAP can map: value boxes and the shape drawings.
   std::vector<visage::Frame *> mapped_;
   std::vector<SourceChip *> chips_;
   std::vector<PitchKnobs> pitchKnobs_;
   struct RouteRow {
      int part, route;
      std::vector<visage::Frame *> frames; // number, source, destination, remove, amount
   };
   std::vector<RouteRow> routeRows_;
   ui::Label *routeInfo_ = nullptr;
   ui::Label *routeEmpty_ = nullptr;
   int routePage_ = 0;
   std::vector<EditorHost::Note> notes_;
   // What put() is building for: a part (-1 for both), and where it shows.
   int part_ = -1;
   Group group_ = kAlways;
   EnvShows envShows_;
   bool mapping_ = false;
   int armed_ = kSourceOff;
   double routeSum_ = 0.0;
   float time_ = 0.0f;
   float envLevel_[kEnvCount] = {};
   float envTime_ = 0.0f;
   int envNote_ = -1;
   dsp::Envelope ledShape_;
   // Each part's envelope editors.
   ui::EnvelopeEditor *envelopes_[kPartCount][kEnvCount] = {};
   int envRanges_[kPartCount][kEnvCount] = {};
   ShapeView *shapes_[kPartCount][kEnvCount] = {};
   double envModes_ = -1.0;
   std::vector<std::pair<float, float>> shapeHeads_;
   std::vector<visage::Frame *> drawings_; // redrawn each frame
   ui::SegmentedSwitch *viewSwitch_ = nullptr;
   int oscTab_ = 0;   // the oscillator panel's tab: 0 OSCILLATORS, 1 FM
   int envTab_ = 0;   // the envelope panel's: 0 ENVELOPES, 1 LFOS
   int voiceTab_ = 0; // and the panel under the sky's: 0 VOICE, 1 FLOCK, 2 MOTION, 3 TOUCH
   ui::TabBar *voiceTabs_ = nullptr;
   // A, B and the main output's peaks, falling slowly, for the level bars.
   float meters_[3] = {};
   std::vector<ui::EnvelopeEditor::Playhead> heads_;
   std::chrono::steady_clock::time_point last_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, const ScreenshotView &view, double seconds, int width, int height,
                      const char *pngPath) {
   Panel panel(host);
   panel.stopTimer();
   panel.showPart(view.part);
   panel.showRoutes(view.routes);
   panel.setMapping(view.mapSource != 0, view.mapSource);
   panel.setView(view.view);
   panel.showVoiceTab(view.voiceTab);
   const int frames = static_cast<int>(seconds * 60.0);
   for (int i = 0; i < frames; ++i)
      panel.step(1.0f / 60.0f);
   panel.syncControls();
   return ui::renderToPng(panel, width, height, pngPath);
}

bool renderFrames(EditorHost &host, const ScreenshotView &view, int width, int height, int frames, double fps,
                  const std::function<void(int)> &advance, const std::function<void(const uint8_t *)> &frame) {
   Panel panel(host);
   panel.stopTimer();
   panel.showPart(view.part);
   panel.showRoutes(view.routes);
   panel.setMapping(view.mapSource != 0, view.mapSource);
   panel.setView(view.view);
   visage::ApplicationEditor editor;
   editor.addChild(&panel);
   editor.onResize() += [&] { panel.setBounds(editor.localBounds()); };
   editor.setWindowless(width, height);
   panel.setBounds(editor.localBounds());
   for (int i = 0; i < frames; ++i) {
      advance(i);
      panel.step(static_cast<float>(1.0 / fps));
      panel.syncControls();
      // Offscreen, nothing draws until asked: draw what this frame changed before taking it.
      editor.drawWindow();
      const visage::Screenshot shot = editor.takeScreenshot();
      if (shot.width() != width || shot.height() != height) {
         editor.removeChild(&panel);
         return false;
      }
      frame(shot.data());
   }
   editor.removeChild(&panel);
   return true;
}

} // namespace jsynth
