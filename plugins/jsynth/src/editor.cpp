// JSynth's panel: the sky fills the window, drawn with bloom, and the controls float over it on
// translucent cards, as JVerb's do. 1200 x 780 design units, scaled to the window.
//
// Every held note is a creature. The panel flies both species' flocks itself, about 60 frames a
// second on the main thread, from the held notes and the flock params, and each creature sheds ink
// that blooms and fades behind it. It is a mock: the audio thread doesn't fly them yet, so nothing
// here reaches the sound.
//
// MAP mode is how modulation is made: press MAP, touch a source (a chip in the header, on the cards
// or under the sky), then drag any knob to set how much that source moves it. Each route shows as a
// ring in the source's colour around the knob it moves.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>

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

constexpr float kDesignWidth = 1200.0f;
constexpr float kDesignHeight = 780.0f;
constexpr float kHeaderHeight = 52.0f;
constexpr float kPi = 3.14159265f;

// The sky the flocks fly in, between the side cards (design units). The flock is simulated in sky
// units: y runs 0..1 down the sky's height and x 0..kSkyAspect across it.
constexpr float kSkyX = 240.0f, kSkyY = 60.0f, kSkyW = 720.0f, kSkyH = 500.0f;
constexpr float kSkyAspect = kSkyW / kSkyH;

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
       0xff7ee0ff, 0xff8fb8ff,                                     // Wingbeat, Wind
       0xff5fd3b0, 0xffc9a0ff,                                     // LFO 1, LFO 2
       0xfff6f0a0, 0xffff6fd2, 0xffb98bff, 0xff8fe8a8, 0xff9ad0c0, // Strike, Press, Glide, Slide, Lift
       0xffc0c8d8,                                                 // Key
       0xfff2c94c, 0xffff8a7a, 0xff6fe0ff, 0xffa98bff, kCaughtColor, 0xff5fd3b0, // the flock's own
       0xffffe08a, 0xff9fd8ff,                                     // X, Y
       0xffd0d0d0, 0xffe0e0ff,                                     // Random, Mod Wheel
       0xfff28c28, 0xff8fe8a8, 0xff6fb8ff, 0xffff6fd2};            // Macros
   return colors[std::clamp(source, 0, kSourceCount - 1)];
}

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
// knob's value, a double-click flips the route's sign and a right-click removes it. Up to four routes
// show as rings outside the knob's own, in their sources' colours, each spanning what it adds.
class MapKnob : public ui::Knob {
public:
   MapKnob(ui::ValueRange range, Mapper &mapper, int part, clap_id offset)
       : Knob(range), mapper_(mapper), part_(part), dest_(destinationIndex(offset)) {}

   void draw(visage::Canvas &c) override {
      Knob::draw(c);
      if (dest_ < 0)
         return;
      rings_.clear();
      mapper_.rings(part_, dest_, rings_);
      const float w = width(), h = height(), s = h / 100.0f;
      const float d = std::min(w * 0.7f, 48.0f * s);
      const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;
      const float sweep = 0.75f * kPi;
      const float from = fraction(value());
      const int armed = mapper_.armedSource();
      bool armedShown = false;
      const int count = std::min<int>(4, static_cast<int>(rings_.size()));
      for (int i = 0; i < count; ++i) {
         const Mapper::Ring &r = rings_[i];
         const float to = std::clamp(from + static_cast<float>(r.amount), 0.0f, 1.0f);
         const float rd = d + (13.0f + 5.0f * i) * s;
         const bool lit = mapper_.mapping() && r.source == armed;
         armedShown |= lit;
         c.setColor(glow(sourceColor(r.source), 0.22f, 1.0f));
         c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 1.4f * s, -0.5f * kPi, sweep, true);
         if (std::fabs(to - from) > 0.002f) {
            const float a0 = -sweep + 2.0f * sweep * from, a1 = -sweep + 2.0f * sweep * to;
            c.setColor(glow(sourceColor(r.source), 1.0f, lit ? 1.6f : 1.0f));
            c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, (lit ? 3.4f : 2.6f) * s, 0.5f * (a0 + a1) - 0.5f * kPi,
                  0.5f * std::fabs(a1 - a0), true);
         }
      }
      // With a source armed, a faint ring where its route would go says "drag me".
      if (mapper_.mapping() && armed != kSourceOff && !armedShown && count < 4) {
         const float rd = d + (13.0f + 5.0f * count) * s;
         c.setColor(glow(sourceColor(armed), 0.55f, 1.0f));
         c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 1.4f * s, -0.5f * kPi, sweep, true);
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
   explicit PresetBar(EditorHost &host) : host_(host) { rescan(); }

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
      if (editor_ && editor_->isVisible())
         return;
      const std::string name = host_.presetName();
      c.setColor(hover_ == Zone::Name ? 0xffffffff : kText);
      c.text(name.empty() ? std::string("Untitled") : name, ui::font(13 * s), visage::Font::kCenter, a, 0,
             w - 2 * a - save, h);
   }

   void mouseMove(const visage::MouseEvent &e) override { setHover(zoneAt(e.position.x)); }
   void mouseExit(const visage::MouseEvent &) override { setHover(Zone::None); }

   void mouseDown(const visage::MouseEvent &e) override {
      switch (zoneAt(e.position.x)) {
      case Zone::Prev: step(-1); break;
      case Zone::Next: step(1); break;
      case Zone::Save: startSave(); break;
      case Zone::Name: showMenu(); break;
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
   enum class Zone { None, Prev, Name, Next, Save };
   static constexpr float kArrow = 24.0f, kSave = 48.0f;
   static constexpr int kSaveId = 1 << 20, kUserBase = 1 << 16;

   Zone zoneAt(float x) const {
      const float s = height() / 26.0f, w = width();
      if (x < kArrow * s)
         return Zone::Prev;
      if (x >= w - kSave * s)
         return Zone::Save;
      if (x >= w - (kSave + kArrow) * s)
         return Zone::Next;
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

   void step(int dir) {
      const int n = total();
      const int current = indexOf(host_.presetName());
      load(current < 0 ? 0 : (current + dir + n) % n);
   }

   void showMenu() {
      rescan();
      visage::PopupMenu menu;
      const std::string current = host_.presetName();
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
      menu.onSelection() += [this](int id) {
         if (id == kSaveId)
            startSave();
         else if (id >= kUserBase && id - kUserBase < static_cast<int>(user_.size()))
            load(static_cast<int>(factoryPresets().size()) + id - kUserBase);
         else if (id >= 0 && id < static_cast<int>(factoryPresets().size()))
            load(id);
      };
      menu.show(this, {kArrow * height() / 26.0f, height()});
   }

   void startSave() {
      if (!editor_) {
         editor_ = std::make_unique<visage::TextEditor>();
         editor_->setJustification(visage::Font::kCenter);
         editor_->onEnterKey() += [this] { finishSave(true); };
         editor_->onEscapeKey() += [this] { finishSave(false); };
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
      if (!editor_ || !editor_->isVisible())
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
   std::vector<Preset> user_;
   int currentIndex_ = -1;
   std::unique_ptr<visage::TextEditor> editor_;
   Zone hover_ = Zone::None;
};

// ---- the sky ----

// Where a held note's lure sits in the sky: low notes left and high right (bend moves it), more Slide
// higher up.
visage::Point lureFor(const EditorHost::Note &n) {
   const float x = std::clamp((n.note + n.bend - 36.0f) / 48.0f, 0.0f, 1.0f);
   return {(0.07f + 0.86f * x) * kSkyAspect, 0.1f + 0.8f * (1.0f - std::clamp(n.timbre, 0.0f, 1.0f))};
}

// One species' flock params, read from the host each frame.
struct Species {
   int behaviour = 0;
   float cohesion = 0, alignment = 0, separation = 0, speed = 0, wander = 0, lure = 0, range = 0;
   float neighbourFm = 0, tailLength = 0, tailSpread = 0, wake = 0;
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
      wake = v(flockParam(kWake));
      wingHz = static_cast<float>(lfoHz(v(lfoParam(0, kLfoRate))));
      wingPush = v(lfoParam(0, kLfoPush));
      windHz = static_cast<float>(lfoHz(v(lfoParam(1, kLfoRate))));
      windPush = v(lfoParam(1, kLfoPush));
   }
   // How near counts as a neighbour, in sky units.
   float radius() const { return 0.08f + 0.32f * range; }
   float maxSpeed(float press) const { return (0.08f + 0.42f * speed) * (1.0f + 0.8f * press); }
};

// A creature's own modulation sources, 0..1, as the stage chips show them.
struct CreatureSources {
   float speed = 0, crowd = 0, nearest = 0, heading = 0, caught = 0, wake = 0, x = 0, y = 0;
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
      // Each species keeps a few resting creatures, dim, that fly while it plays nothing.
      for (int part = 0; part < kPartCount; ++part)
         for (int i = 0; i < kIdle; ++i)
            spawn(part, -1, {(0.15f + 0.3f * u(rng_) + 0.4f * part) * kSkyAspect, 0.25f + 0.5f * u(rng_)});
      puffs_.reserve(kMaxPuffs);
   }

   // The part whose creatures are drawn in front, brighter, and whose sources the chips show.
   int shownPart = 0;
   // A line of help at the top of the sky (MAP mode's), in its colour.
   std::string hint;
   unsigned hintColor = kText;

   // One frame, `dt` seconds after the last.
   void advance(const std::vector<EditorHost::Note> &notes, float dt) {
      time_ += dt;
      for (int part = 0; part < kPartCount; ++part)
         species_[part].read(host_, part);
      follow(notes, dt);
      fly(dt);
      shed(dt);
      moveInk(dt);
      measure(dt);
      redraw();
   }

   // The sources of `part`'s newest held creature, or a resting one's when it plays nothing.
   CreatureSources sources(int part) const {
      const Creature *pick = nullptr;
      for (const Creature &b : creatures_)
         if (b.part == part && (b.note >= 0 || (!pick && b.note == -1)))
            pick = &b;
      return pick ? pick->sources : CreatureSources{};
   }

   void draw(visage::Canvas &c) override {
      const float s = width() / designWidth_;
      bloom_.setBloomSize(18.0f * s * (dpiScale() > 0.0f ? dpiScale() : 1.0f));
      drawBackdrop(c, s);
      const float ppu = kSkyH * s;
      const float ox = kSkyX * s, oy = (kSkyY - kHeaderHeight) * s;
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

      // Links: neighbours of a kind feed each other FM, brighter the nearer they fly. A hunter that
      // has caught its prey bites it, in pink.
      for (size_t i = 0; i < creatures_.size(); ++i)
         for (size_t j = i + 1; j < creatures_.size(); ++j) {
            const Creature &a = creatures_[i], &b = creatures_[j];
            const float d = std::hypot(a.x - b.x, a.y - b.y), e = std::min(a.energy, b.energy);
            if (a.part == b.part) {
               const Species &sp = species_[a.part];
               const float R = sp.radius() * 0.6f;
               if (d < R && e > 0.05f) {
                  const float k = (1.0f - d / R) * e * (0.15f + 0.6f * sp.neighbourFm);
                  c.setColor(glow(kText, k * (a.part == shownPart ? 0.7f : 0.35f), 1.2f));
                  c.segment(X(a.x), Y(a.y), X(b.x), Y(b.y), 1.0f * s, true);
               }
            }
            else if (d < kCatch * 2.0f && e > 0.2f && (a.sources.caught > 0.3f || b.sources.caught > 0.3f)) {
               c.setColor(glow(kCaughtColor, 0.9f * e, 1.8f));
               c.segment(X(a.x), Y(a.y), X(b.x), Y(b.y), 2.2f * s, true);
            }
         }

      // Lures: where each held note pulls its creature.
      for (const Creature &b : creatures_) {
         if (b.note < 0 || b.energy < 0.05f)
            continue;
         const unsigned col = kPartColors[b.part];
         c.setColor(glow(col, 0.14f * b.energy, 1.0f));
         c.segment(X(b.lureX), Y(b.lureY), X(b.x), Y(b.y), 1.0f * s, true);
         c.setColor(glow(col, 0.55f * b.energy, 1.0f));
         const float r = (9.0f + 5.0f * b.press) * s;
         c.ring(X(b.lureX) - r, Y(b.lureY) - r, 2 * r, 1.2f * s);
      }

      // The other species first, so the shown one flies in front.
      for (int pass = 0; pass < 2; ++pass)
         for (const Creature &b : creatures_)
            if ((b.part == shownPart) == (pass == 1))
               drawCreature(c, b, s, X, Y);

      if (!hint.empty()) {
         c.setColor(glow(hintColor, 0.95f, 1.0f));
         c.text(hint, ui::font(12 * s), visage::Font::kCenter, kSkyX * s, 6 * s, kSkyW * s, 18 * s);
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
   static constexpr int kIdle = 6;
   static constexpr int kMaxPuffs = 2600;
   static constexpr int kPathPoints = 22;
   static constexpr float kCatch = 0.025f;

   struct Creature {
      int id = 0;
      int part = 0;
      // The note it follows; -1 resting, and -2 - note once that note is let go and it fades.
      int note = -1;
      unsigned color = 0;
      float x = 0, y = 0, vx = 0, vy = 0;
      float energy = 0;
      float lureX = 0, lureY = 0, press = 0;
      float wanderAngle = 0, wing = 0, emit = 0;
      CreatureSources sources;
      std::vector<visage::Point> path;
   };
   struct Puff {
      float x, y, vx, vy, age, life, size, grow, alpha;
      unsigned color;
      int part;
      // The id of the creature that shed it: a creature feels only others' wake, and only other
      // creatures' ink pushes back.
      int owner;
      // How hard it is pressed against other ink (it glows whiter), and how many puffs overlap it
      // (each draws thinner, so a dense knot stays a cloud).
      float heat = 0.0f, crowd = 0.0f;
   };
   struct Star {
      float x, y, b, phase;
   };

   bool skyAt(visage::Point p, float &nx, float &timbre) const {
      const float s = width() / designWidth_;
      const float x = (p.x / s - kSkyX) / kSkyH, y = (p.y / s + kHeaderHeight - kSkyY) / kSkyH;
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
         return (n.strike * 127.0f >= static_cast<float>(host_.paramValue(kVelocitySplit))) == (part == 1);
      default: // Layer, zones and the crossfade play both
         return true;
      }
   }

   Creature &spawn(int part, int note, visage::Point at) {
      std::uniform_real_distribution<float> u(-1.0f, 1.0f);
      Creature b;
      b.id = nextId_++;
      b.part = part;
      b.note = note;
      b.color = kInks[part][(note < 0 ? static_cast<int>(creatures_.size()) : note * 5) & 3];
      b.x = at.x + 0.03f * u(rng_);
      b.y = at.y + 0.03f * u(rng_);
      b.vx = 0.1f * u(rng_);
      b.vy = 0.1f * u(rng_);
      b.wanderAngle = 3.14f * u(rng_);
      b.wing = 3.14f * u(rng_);
      b.path.reserve(kPathPoints);
      creatures_.push_back(b);
      return creatures_.back();
   }

   // Matches creatures to the held notes: a new note hatches one at its lure, a released note's
   // creature flies off and fades, and resting creatures show only while a species plays nothing.
   void follow(const std::vector<EditorHost::Note> &notes, float dt) {
      for (Creature &b : creatures_)
         if (b.note >= 0)
            b.note = -2 - b.note; // let go, unless a held note claims it below
      int held[kPartCount] = {};
      for (const EditorHost::Note &n : notes)
         for (int part = 0; part < kPartCount; ++part) {
            if (!plays(part, n))
               continue;
            ++held[part];
            const visage::Point lure = lureFor(n);
            Creature *b = nullptr;
            for (Creature &c : creatures_)
               if (c.part == part && c.note == -2 - n.note)
                  b = &c;
            if (!b)
               b = &spawn(part, n.note, lure);
            b->note = n.note;
            b->lureX = lure.x;
            b->lureY = lure.y;
            b->press = std::clamp(n.pressure, 0.0f, 1.0f);
         }
      for (Creature &b : creatures_) {
         const float target = b.note >= 0 ? 0.55f + 0.45f * b.press : b.note == -1 && held[b.part] == 0 ? 0.3f : 0.0f;
         const float rate = b.note >= 0 ? 5.0f : 1.2f;
         b.energy += (target - b.energy) * std::min(1.0f, dt * rate);
      }
      creatures_.erase(std::remove_if(creatures_.begin(), creatures_.end(),
                                      [](const Creature &b) { return b.note <= -2 && b.energy < 0.01f; }),
                       creatures_.end());
   }

   // One boids step for every creature.
   void fly(float dt) {
      std::uniform_real_distribution<float> u(-1.0f, 1.0f);
      const int walls = static_cast<int>(host_.paramValue(kSkyWalls) + 0.5);
      // The wind: one slow gust for the whole sky, from part B's Wind rate (the two parts share it).
      const float windAngle = 0.4f + 0.9f * std::sin(time_ * 0.07f);
      const float gust = std::sin(2.0f * kPi * species_[1].windHz * time_ + 1.0f);
      for (Creature &b : creatures_) {
         const Species &sp = species_[b.part];
         const float R = sp.radius();
         float cx = 0, cy = 0, ax = 0, ay = 0, sx = 0, sy = 0, hx = 0, hy = 0;
         float nearest = 1e9f, prey = 1e9f;
         int n = 0;
         for (const Creature &o : creatures_) {
            if (&o == &b || o.energy < 0.05f)
               continue;
            const float dx = o.x - b.x, dy = o.y - b.y, d = std::hypot(dx, dy) + 1e-5f;
            nearest = std::min(nearest, d);
            if (o.part == b.part) {
               if (d < R) {
                  cx += o.x, cy += o.y, ax += o.vx, ay += o.vy, ++n;
               }
               if (d < 0.4f * R) {
                  const float k = 1.0f - d / (0.4f * R);
                  sx -= dx / d * k, sy -= dy / d * k;
               }
            }
            else {
               // Hunters chase the nearest of the other species; everyone shies from a hunter, and
               // Flee runs from the other species whatever it does.
               if (sp.behaviour == 2 && d < prey && d < 2.5f * R) {
                  prey = d;
                  hx = dx / d, hy = dy / d;
               }
               const float scare = sp.behaviour == 3 ? 1.5f : species_[o.part].behaviour == 2 ? 0.6f : 0.0f;
               if (scare > 0.0f && d < R) {
                  sx -= dx / d * scare * (1.0f - d / R);
                  sy -= dy / d * scare * (1.0f - d / R);
               }
            }
         }
         float accX = 0, accY = 0;
         if (n > 0) {
            const float pull = sp.behaviour == 1 ? -1.0f : 1.0f; // Scatter pushes away from the middle
            accX += (cx / n - b.x) * sp.cohesion * 2.5f * pull + (ax / n - b.vx) * sp.alignment * 2.0f;
            accY += (cy / n - b.y) * sp.cohesion * 2.5f * pull + (ay / n - b.vy) * sp.alignment * 2.0f;
         }
         accX += sx * sp.separation * 1.6f + hx * 0.9f;
         accY += sy * sp.separation * 1.6f + hy * 0.9f;
         if (b.note >= 0) {
            accX += (b.lureX - b.x) * sp.lure * 6.0f - b.vx * sp.lure * 0.6f;
            accY += (b.lureY - b.y) * sp.lure * 6.0f - b.vy * sp.lure * 0.6f;
         }
         b.wanderAngle += u(rng_) * dt * 5.0f;
         accX += std::cos(b.wanderAngle) * sp.wander * 0.5f;
         accY += std::sin(b.wanderAngle) * sp.wander * 0.5f;
         accX += std::cos(windAngle) * gust * sp.windPush * 0.35f;
         accY += std::sin(windAngle) * gust * sp.windPush * 0.35f;
         // The wingbeat: each creature bobs across its path, faster when pressed.
         b.wing += 2.0f * kPi * std::min(sp.wingHz, 12.0f) * dt * (1.0f + b.press);
         const float v = std::hypot(b.vx, b.vy) + 1e-5f;
         accX += -b.vy / v * std::sin(b.wing) * sp.wingPush * 0.8f;
         accY += b.vx / v * std::sin(b.wing) * sp.wingPush * 0.8f;
         // Walls: soft ones turn creatures back near the edge; open ones only well past it.
         const float m = walls == 2 ? -0.08f : 0.06f, k = walls == 2 ? 4.0f : 14.0f;
         if (walls != 1) {
            if (b.x < m)
               accX += (m - b.x) * k;
            if (b.x > kSkyAspect - m)
               accX -= (b.x - kSkyAspect + m) * k;
            if (b.y < m)
               accY += (m - b.y) * k;
            if (b.y > 1.0f - m)
               accY -= (b.y - 1.0f + m) * k;
         }
         b.vx += accX * dt;
         b.vy += accY * dt;
         const float top = sp.maxSpeed(b.press), speed = std::hypot(b.vx, b.vy) + 1e-6f;
         const float clamped = std::clamp(speed, 0.35f * top, top);
         b.vx *= clamped / speed;
         b.vy *= clamped / speed;
         b.x += b.vx * dt;
         b.y += b.vy * dt;
         if (walls == 1) {
            const float wrapX = std::fmod(b.x + kSkyAspect, kSkyAspect), wrapY = std::fmod(b.y + 1.0f, 1.0f);
            if (wrapX != b.x || wrapY != b.y)
               b.path.clear();
            b.x = wrapX, b.y = wrapY;
         }
         if (static_cast<int>(b.path.size()) == kPathPoints)
            b.path.erase(b.path.begin());
         b.path.push_back({b.x, b.y});
         b.sources.speed = clamped / sp.maxSpeed(1.0f);
         b.sources.crowd = std::min(1.0f, n / 6.0f);
         b.sources.nearest = std::clamp(1.0f - nearest / R, 0.0f, 1.0f);
         b.sources.heading = (std::atan2(b.vy, b.vx) + kPi) / (2.0f * kPi);
         b.sources.x = std::clamp(b.x / kSkyAspect, 0.0f, 1.0f);
         b.sources.y = std::clamp(1.0f - b.y, 0.0f, 1.0f);
      }
      // A hunter that reaches its prey has caught it, and holds on for a moment.
      for (Creature &b : creatures_) {
         b.sources.caught *= std::exp(-dt * 2.0f);
         for (const Creature &o : creatures_)
            if (o.part != b.part && species_[o.part].behaviour == 2 && o.energy > 0.1f && b.energy > 0.1f &&
                std::hypot(o.x - b.x, o.y - b.y) < kCatch)
               b.sources.caught = 1.0f;
      }
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
   // creature flying through ink stirs it along: that stirring is the wake a creature feels.
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
               const float k = (1.0f - std::sqrt(d2) / 0.05f) * species_[b.part].wake * 3.0f * dt;
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

   // How much of the other creatures' fresh ink each one is flying through.
   void measure(float dt) {
      for (Creature &b : creatures_) {
         float ink = 0.0f;
         for (const Puff &p : puffs_)
            if (p.owner != b.id && std::fabs(p.x - b.x) < 0.04f && std::fabs(p.y - b.y) < 0.04f)
               ink += p.alpha * (1.0f - p.age / p.life);
         b.sources.wake += (std::min(1.0f, ink / 12.0f) - b.sources.wake) * std::min(1.0f, dt * 6.0f);
      }
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
      const float flap = 0.45f + 0.35f * std::sin(b.wing);
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
      const float x0 = kSkyX * s, y0 = (kSkyY - kHeaderHeight) * s, x1 = x0 + kSkyW * s, y1 = y0 + kSkyH * s, t = 12 * s;
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
   std::vector<Puff> puffs_;
   std::vector<std::vector<int>> grid_;
   Star stars_[150];
   int nextId_ = 0;
   float time_ = 0.0f;
   int playing_ = -1;
};

// ---- the panel ----

class Panel : public ui::DesignFrame, public visage::EventTimer, public Mapper {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      view_ = &add<FlockView>({0, kHeaderHeight, kDesignWidth, kDesignHeight - kHeaderHeight}, host, kDesignWidth);
      // The sky fills the panel below the header, so its right-click opens the UI size menu the
      // panel's own background would.
      view_->onBackgroundMenu = [this](visage::Point p) { showZoomMenu(*view_, p); };
      buildHeader();
      buildStageChips();
      buildLeft();
      buildRight();
      for (int part = 0; part < kPartCount; ++part) {
         part_ = part;
         buildOscs(part);
         buildVoice(part);
         buildRoutes(part);
      }
      part_ = -1;
      buildRouteControls();
      showTabs(host_.leftTab, host_.rightTab);
      showRoutes(host_.routesOpen);
      showPart(host_.part);
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
      // The chips' levels: the newest note's touch, and the shown species' newest creature.
      float touch[kTouchCount] = {};
      if (!notes_.empty()) {
         const EditorHost::Note &n = notes_.back();
         touch[0] = n.strike;
         touch[1] = n.pressure;
         touch[2] = std::min(1.0f, std::fabs(n.bend) / 2.0f);
         touch[3] = n.timbre;
         touch[4] = n.lift;
      }
      runEnvelopes(dt);
      const CreatureSources src = view_->sources(host_.part);
      const float flock[] = {src.speed, src.crowd, src.nearest, src.heading, src.caught, src.wake, src.x, src.y};
      const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.0f);
      for (SourceChip *chip : chips_) {
         const int s = chip->source();
         if (s >= kSourceStrike && s < kSourceStrike + kTouchCount)
            chip->setLevel(touch[s - kSourceStrike]);
         else if (s >= kSourceSpeed && s <= kSourceSkyY)
            chip->setLevel(flock[s - kSourceSpeed]);
         else if (s >= kSourceEnv1 && s < kSourceEnv1 + kEnvCount)
            chip->setLevel(envLevel_[s - kSourceEnv1]);
         else if (s >= kSourceWingbeat && s < kSourceWingbeat + kLfoCount)
            chip->setLevel(0.5f + 0.5f * lfoValue(s - kSourceWingbeat), true);
         chip->setState(mapping_, mapping_ && s == armed_, pulse);
      }
      refreshPitch();
      // Rings follow the routes, which the host or the route list can change too.
      const double sum = routeChecksum();
      if (sum != routeSum_) {
         routeSum_ = sum;
         layoutRoutes();
         for (MapKnob *k : mapKnobs_)
            k->redraw();
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
   void showTabs(int left, int right) {
      leftTabs_->select(host_.leftTab = std::clamp(left, 0, 2));
      rightTabs_->select(host_.rightTab = std::clamp(right, 0, 3));
      refresh();
   }
   void showRoutes(bool open) {
      host_.routesOpen = open;
      routesButton_->setOn(open);
      refresh();
   }
   // MAP on or off, with `source` armed.
   void setMapping(bool on, int source) {
      mapping_ = on;
      armed_ = on ? source : kSourceOff;
      mapButton_->setOn(on);
      for (MapKnob *k : mapKnobs_)
         k->redraw();
   }

   void draw(visage::Canvas &c) override {
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
   // Where a widget shows: always, or on a tab of the left or right card, or in the route list.
   enum Group { kAlways, kLeft, kRight, kRoutes };
   struct Placed {
      visage::Frame *frame;
      int part; // -1 for both
      Group group;
      int tab;
   };
   struct PitchKnobs {
      int part, osc;
      ui::Knob *coarse, *ratio, *fixed;
   };

   double value(int part, clap_id offset) const { return host_.paramValue(partId(part, offset)); }

   // The shown part's LFO `l` now, -1..1, free running from its rate, shape and phase, for its LED.
   float lfoValue(int l) const {
      const int part = host_.part;
      const double cycles = time_ * lfoHz(value(part, lfoParam(l, kLfoRate))) + value(part, lfoParam(l, kLfoPhase)) / 360.0;
      const float ph = static_cast<float>(cycles - std::floor(cycles));
      float v;
      switch (static_cast<int>(value(part, lfoParam(l, kLfoShape)) + 0.5)) {
      case 1: v = 1.0f - 4.0f * std::fabs(ph - 0.5f); break;          // triangle
      case 2: v = 2.0f * ph - 1.0f; break;                             // saw
      case 3: v = ph < 0.5f ? 1.0f : -1.0f; break;                     // square
      case 4: v = std::sin(static_cast<float>(std::floor(cycles)) * 12.9898f) > 0.0f ? 0.7f : -0.7f; break; // S&H
      case 5: v = 0.6f * std::sin(2.0f * kPi * ph) + 0.4f * std::sin(2.0f * kPi * ph * 0.37f + 1.0f); break; // smooth
      default: v = std::sin(2.0f * kPi * ph); break;                   // sine
      }
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
      for (const Placed &p : placed_) {
         bool on = p.part < 0 || p.part == host_.part;
         if (p.group == kLeft)
            on = on && leftTabs_->selected() == p.tab;
         else if (p.group == kRight)
            on = on && rightTabs_->selected() == p.tab;
         else if (p.group == kRoutes)
            on = on && host_.routesOpen;
         p.frame->setVisible(on);
      }
      refreshPitch();
      if (routeInfo_)
         layoutRoutes();
      redraw();
   }
   // Shows each oscillator's pitch knob for its pitch mode.
   void refreshPitch() {
      for (const PitchKnobs &p : pitchKnobs_) {
         const int mode = static_cast<int>(value(p.part, oscParam(p.osc, kOscPitchMode)) + 0.5);
         const bool shown = p.part == host_.part;
         p.coarse->setVisible(shown && mode == 0);
         p.ratio->setVisible(shown && mode == 1);
         p.fixed->setVisible(shown && mode == 2);
      }
   }

   // Adds a widget for the part, card and tab being built.
   template <typename T, typename... Args>
   T &put(ui::Rect bounds, Args &&...args) {
      T &frame = add<T>(bounds, std::forward<Args>(args)...);
      placed_.push_back({&frame, part_, group_, tab_});
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
   void label(ui::Rect r, const std::string &t, unsigned color = kText,
              visage::Font::Justification j = visage::Font::kCenter) {
      put<ui::Label>(r, t, color, j);
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
   ui::SegmentedSwitch &segments(clap_id offset, unsigned accent, ui::Rect r, std::vector<std::string> names) {
      const clap_id id = idOf(offset);
      auto &sw = glassy(put<ui::SegmentedSwitch>(r, std::move(names)), accent);
      bindings_.bind(sw, id);
      return sw;
   }
   // A source to touch in MAP mode; touching the armed one again lets it go.
   SourceChip &chip(int source, const std::string &name, ui::Rect r, const char *tip = nullptr) {
      auto &c = put<SourceChip>(r, source, name);
      c.onClick = [this](int s) {
         if (mapping_)
            setMapping(true, armed_ == s ? kSourceOff : s);
      };
      chips_.push_back(&c);
      if (tip)
         tips_.add(c, tip);
      return c;
   }

   void buildHeader() {
      put<PresetBar>({262, 13, 250, 26}, host_);
      auto &parts = glassy(put<ui::SegmentedSwitch>({522, 13, 146, 26}, std::vector<std::string>{"PART A", "PART B"}),
                           kPartColors[0]);
      parts.onChange = [this](int p) { showPart(p); };
      partSwitch_ = &parts;
      tips_.add(parts, "Each part is a species with its own patch and its own way of flying. The cards edit "
                       "this one; the sky shows both.");
      auto &map = glassy(put<ui::Button>({800, 13, 62, 26}, "MAP"), kMapColor);
      map.setToggle(true);
      map.onToggle = [this](bool on) { setMapping(on, kSourceOff); };
      mapButton_ = &map;
      tips_.add(map, "Press MAP, touch a source, then drag any knob to set how much the source moves it. "
                     "Double-click a knob to flip its route, right-click to remove it.");
      const auto cells = ui::row({872, 13, 312, 26}, kTouchCount, 4);
      for (int i = 0; i < kTouchCount; ++i)
         chip(kSourceStrike + i, upper(kTouchNames[i]), cells[i]);
      auto &mode = glassy(put<ui::Dropdown>({678, 13, 112, 26}, upper(kPlayModeNames)), kPartColors[0]);
      bindings_.bind(mode, kPlayMode);
      tips_.add(mode, "Which species a note hatches: A only, both, split by key or by velocity, by MPE zone, "
                      "or crossfaded by Slide or Press.");
      // ROUTES sits under the sky, at the end of the flock's own sources.
      auto &routes = glassy(put<ui::Button>({836, 571, 116, 24}, "ROUTES"), kMapColor);
      routes.setToggle(true);
      routes.onToggle = [this](bool on) { showRoutes(on); };
      routesButton_ = &routes;
      tips_.add(routes, "Every route of this part as a list, to edit by name.");
   }

   // The flock's own sources, under the sky.
   void buildStageChips() {
      const std::pair<int, const char *> sources[] = {{kSourceSpeed, "SPEED"},     {kSourceCrowd, "CROWD"},
                                                      {kSourceNearest, "NEAREST"}, {kSourceHeading, "HEADING"},
                                                      {kSourceCaught, "CAUGHT"},   {kSourceWake, "WAKE"},
                                                      {kSourceSkyX, "X"},          {kSourceSkyY, "Y"}};
      const char *tips[] = {"How fast this creature flies.", "How many neighbours it has.",
                            "How near its nearest neighbour is.", "Which way it flies, all the way round.",
                            "On while a hunter has caught it.", "How much of the others' ink it flies through.",
                            "Where it is in the sky, left to right.", "Where it is in the sky, bottom to top."};
      const auto cells = ui::row({248, 572, 578, 22}, 8, 4);
      for (int i = 0; i < 8; ++i)
         chip(sources[i].first, sources[i].second, cells[i], tips[i]);
   }

   // Left: how a species flies, its FM, and the touch response.
   void buildLeft() {
      glass({16, 64, 214, 536}, "", kDim);
      leftTabs_ = &glassy(put<ui::TabBar>({24, 70, 198, 26}, std::vector<std::string>{"FLOCK", "FM", "TOUCH"}), kDim);
      leftTabs_->onChange = [this](int t) {
         host_.leftTab = t;
         refresh();
      };
      group_ = kLeft;
      for (int part = 0; part < kPartCount; ++part) {
         part_ = part;
         const unsigned c = kPartColors[part];
         tab_ = 0;
         auto &behaviour = dropdown(flockParam(kBehaviour), c, {30, 106, 186, 24});
         tips_.add(behaviour, "SHOAL flies with its own kind, SCATTER keeps apart, HUNT chases the other species "
                              "and FLEE runs from it.");
         const auto g = ui::grid({22, 138, 202, 448}, 3, 4);
         knob(flockParam(kCohesion), "COHESION", c, g[0], "Pull toward the middle of the neighbours.");
         knob(flockParam(kAlignment), "ALIGN", c, g[1], "Turn to fly the way the neighbours fly.");
         knob(flockParam(kSeparation), "SEPARATE", c, g[2], "Keep clear of the nearest.");
         knob(flockParam(kSpeed), "SPEED", c, g[3]);
         knob(flockParam(kWander), "WANDER", c, g[4], "Each creature's own restlessness.");
         knob(flockParam(kLure), "LURE", c, g[5], "How hard your finger pulls its creature.");
         knob(flockParam(kRange), "RANGE", c, g[6], "How near counts as a neighbour.");
         knob(flockParam(kTailLength), "TAIL", c, g[7], "How long the ink lasts behind a creature.");
         knob(flockParam(kTailSpread), "SPREAD", c, g[8], "How far the ink blooms as it ages.");
         knob(flockParam(kWake), "WAKE", c, g[9], "How much a creature stirs the ink it flies through, and so "
                                                   "how much WAKE the others feel.");
         if (part == 0) {
            part_ = -1;
            knob(kSkySize, "SIZE", kDim, g[10], "How big the sky is, so how far apart the flocks can spread.");
            label({g[11].x, g[11].y + 2, g[11].w, 16}, "WALLS");
            auto &walls = dropdown(kSkyWalls, kDim, {g[11].x + 4, g[11].y + 34, g[11].w - 8, 24});
            tips_.add(walls, "What the sky's edges do: turn creatures back, wrap them round, or let them roam.");
            part_ = part;
         }

         tab_ = 1;
         // The FM grid: the row is the oscillator modulating, the column the one modulated, and the
         // diagonal each one's feedback.
         label({30, 102, 186, 16}, "OSC FM, ROW INTO COLUMN", kDim);
         const auto f = ui::grid({22, 118, 202, 252}, 3, 3);
         static const char *names[3][3] = {{"FB 1", "1 > 2", "1 > 3"}, {"2 > 1", "FB 2", "2 > 3"}, {"3 > 1", "3 > 2", "FB 3"}};
         for (int from = 0; from < kOscCount; ++from)
            for (int to = 0; to < kOscCount; ++to)
               knob(fmCell(from, to), names[from][to], kOscColors[from], f[from * 3 + to]);
         segments(kFmBase + kFmType, c, {30, 374, 186, 22}, std::vector<std::string>{"PHASE", "EXP"});
         label({30, 406, 186, 16}, "BETWEEN CREATURES", kDim);
         const auto x = ui::row({22, 424, 202, 94}, 3);
         knob(flockParam(kNeighbourFm), "NEIGHBOUR", c, x[0], "The nearest neighbour's Osc 1 into this creature's "
                                                               "Osc 2: a tight flock rings, a loose one is clean.");
         knob(flockParam(kTunePull), "TUNE PULL", c, x[1], "Creatures flying the same way drift toward pure "
                                                            "intervals.");
         knob(flockParam(kBite), "BITE", c, x[2], "Ring mod while a hunter holds this creature.");
         const auto y = ui::row({22, 518, 202, 78}, 3);
         knob(mixParam(kNoiseLevel), "NOISE", c, y[0]);
         knob(mixParam(kNoiseColor), "COLOR", c, y[1]);
         knob(kDrift, "DRIFT", c, y[2], "Each creature's own slow wander in pitch.");
      }
      part_ = -1;

      // The touch response, shared by both parts.
      tab_ = 2;
      label({88, 102, 66, 16}, "CURVE", kDim);
      label({154, 102, 66, 16}, "SMOOTH", kDim);
      for (int i = 0; i < kTouchCount; ++i) {
         const float y = 116 + i * 74.0f;
         label({28, y + 26, 62, 16}, upper(kTouchNames[i]), sourceColor(kSourceStrike + i), visage::Font::kLeft);
         knob(kTouchCurve1 + i, "", sourceColor(kSourceStrike + i), {88, y, 66, 74});
         knob(kTouchSmooth1 + i, "", sourceColor(kSourceStrike + i), {154, y, 66, 74});
      }
      const auto r = ui::row({22, 492, 202, 100}, 3);
      knob(kGlideRounding, "ROUND", kDim, r[0], "Pulls a resting finger to the nearest semitone.");
      knob(kRoundingTime, "ROUND TIME", kDim, r[1]);
      knob(kManagerBendRange, "MGR BEND", kDim, r[2], "The MPE manager channel's bend range.");
      group_ = kAlways;
      tab_ = 0;
   }

   // Right: the envelopes, the wingbeat and the wind, and the filters.
   void buildRight() {
      glass({970, 64, 214, 536}, "", kDim);
      rightTabs_ = &glassy(put<ui::TabBar>({978, 70, 198, 26}, std::vector<std::string>{"ENV", "MOTION", "LFO", "FILTER"}), kDim);
      rightTabs_->onChange = [this](int t) {
         host_.rightTab = t;
         refresh();
      };
      group_ = kRight;
      tab_ = 0;
      const char *envNames[kEnvCount] = {"ENV 1  AMP", "ENV 2", "ENV 3"};
      for (int e = 0; e < kEnvCount; ++e)
         chip(kSourceEnv1 + e, envNames[e], {982, 104 + e * 164.0f, 104, 22});
      tab_ = 1;
      chip(kSourceWingbeat, "WINGBEAT", {982, 104, 104, 22}, "One per creature: each bobs across its path.");
      chip(kSourceWind, "WIND", {982, 352, 104, 22}, "One gust for the whole sky, pushing every creature.");
      tab_ = 2;
      chip(kSourceLfo1, "LFO 1", {982, 104, 104, 22}, "A plain LFO, one per creature unless its trigger is Global.");
      chip(kSourceLfo1 + 1, "LFO 2", {982, 352, 104, 22}, "A plain LFO, one per creature unless its trigger is Global.");
      for (int part = 0; part < kPartCount; ++part) {
         part_ = part;
         const unsigned c = kPartColors[part];
         tab_ = 0;
         for (int e = 0; e < kEnvCount; ++e) {
            const float y = 104 + e * 164.0f;
            const auto id = [&](EnvParam p) { return partId(part, envParam(e, p)); };
            auto &env = glassy(put<ui::EnvelopeEditor>({980, y + 30, 194, 120}, rangeOf(id(kEnvAttack)),
                                                       rangeOf(id(kEnvDecay)), rangeOf(id(kEnvSustain)),
                                                       rangeOf(id(kEnvRelease))),
                               sourceColor(kSourceEnv1 + e));
            env.setTimeCurve(0.35f);
            bindings_.bind(env, id(kEnvAttack), id(kEnvDecay), id(kEnvSustain), id(kEnvRelease));
            dropdown(envParam(e, kEnvLoop), c, {1094, y, 80, 22});
         }
         // MOTION has the wingbeat and the wind, which also move the flock; LFO the two that don't.
         for (int l = 0; l < kLfoCount; ++l) {
            tab_ = l < kMotionCount ? 1 : 2;
            const float y = 104 + (l % 2) * 248.0f;
            const unsigned lc = sourceColor(kSourceWingbeat + l);
            dropdown(lfoParam(l, kLfoShape), lc, {1094, y, 80, 22});
            dropdown(lfoParam(l, kLfoTrigger), c, {982, y + 28, 104, 22});
            segments(lfoParam(l, kLfoSync), lc, {1094, y + 28, 80, 22}, std::vector<std::string>{"FREE", "SYNC"});
            const auto k = ui::grid({976, y + 56, 202, 184}, 3, 2);
            // The plain LFOs have no Push, so their cells move up one.
            const int at = l < kMotionCount ? 0 : -1;
            knob(lfoParam(l, kLfoRate), "RATE", lc, k[0]);
            if (l < kMotionCount)
               knob(lfoParam(l, kLfoPush), "PUSH", lc, k[1], l == 0 ? "How far each creature bobs." : "How hard the gust pushes.");
            knob(lfoParam(l, kLfoPhase), "PHASE", lc, k[2 + at]);
            knob(lfoParam(l, kLfoDelay), "DELAY", lc, k[3 + at]);
            const ui::Rect dv = k[4 + at], pl = k[5 + at];
            label({dv.x, dv.y + 6, dv.w, 16}, "DIVISION");
            dropdown(lfoParam(l, kLfoDivision), lc, {dv.x + 4, dv.y + 34, dv.w - 8, 22});
            label({pl.x, pl.y + 6, pl.w, 16}, "POLARITY");
            dropdown(lfoParam(l, kLfoPolarity), lc, {pl.x + 4, pl.y + 34, pl.w - 8, 22});
         }
         tab_ = 3;
         segments(tailParam(kFilterRouting), c, {982, 104, 190, 22}, std::vector<std::string>{"SERIAL", "PARALLEL", "SPLIT"});
         for (int f = 0; f < kFilterCount; ++f) {
            const float y = 136 + f * 150.0f;
            label({984, y + 3, 56, 16}, f == 0 ? "FILTER 1" : "FILTER 2", c, visage::Font::kLeft);
            dropdown(filterParam(f, kFilterType), c, {1040, y, 84, 22});
            dropdown(filterParam(f, kFilterSlope), c, {1128, y, 46, 22});
            const auto k = ui::row({976, y + 28, 202, 96}, 4);
            knob(filterParam(f, kFilterCutoff), "CUTOFF", c, k[0]);
            knob(filterParam(f, kFilterResonance), "RES", c, k[1]);
            knob(filterParam(f, kFilterDrive), "DRIVE", c, k[2]);
            knob(filterParam(f, kFilterKeyTrack), "KEY", c, k[3]);
         }
         label({984, 439, 56, 16}, "DRIVE", c, visage::Font::kLeft);
         dropdown(tailParam(kDriveType), c, {1040, 436, 134, 22});
         const auto k = ui::row({976, 464, 202, 100}, 3);
         knob(tailParam(kDriveAmount), "DRIVE", c, k[0]);
         knob(tailParam(kFilterBalance), "BALANCE", c, k[1], "Filter 1 against Filter 2, in parallel and split.");
         knob(kVelocityToAmp, "VELOCITY", c, k[2], "How much Strike sets the level.");
      }
      part_ = -1;
      group_ = kAlways;
      tab_ = 0;
   }

   // Bottom: a card per oscillator.
   void buildOscs(int part) {
      const float w = 250, gap = 10, y = 612, h = 152;
      for (int o = 0; o < kOscCount; ++o) {
         const float x = 16 + o * (w + gap);
         const unsigned c = kOscColors[o];
         glass({x, y, w, h}, "", c);
         label({x + 14, y + 9, 50, 18}, "OSC " + std::to_string(o + 1), c, visage::Font::kLeft);
         auto &pitch = segments(oscParam(o, kOscPitchMode), c, {x + 60, y + 7, 100, 22},
                                std::vector<std::string>{"NOTE", "RATIO", "HZ"});
         tips_.add(pitch, "NOTE follows the key in semitones; RATIO is a multiple of the note; HZ is fixed.");
         dropdown(oscParam(o, kOscWave), c, {x + 164, y + 7, 78, 22});
         const auto k = ui::row({x + 4, y + 34, w - 8, 112}, 5);
         // The pitch knob shows whichever the pitch mode uses; refreshPitch() swaps them.
         auto &coarse = knob(oscParam(o, kOscCoarse), "COARSE", c, k[0]);
         auto &ratio = knob(oscParam(o, kOscRatio), "RATIO", c, k[0]);
         auto &fixed = knob(oscParam(o, kOscFixed), "FREQ", c, k[0]);
         pitchKnobs_.push_back({part, o, &coarse, &ratio, &fixed});
         knob(oscParam(o, kOscFine), "FINE", c, k[1]);
         knob(oscParam(o, kOscShape), "SHAPE", c, k[2]);
         knob(mixParam(kMixLevel, o), "LEVEL", c, k[3]);
         knob(mixParam(kMixPan, o), "PAN", c, k[4]);
      }
   }

   // Bottom right: the part's voice.
   void buildVoice(int part) {
      const unsigned c = kPartColors[part];
      const float x = 796, y = 612;
      glass({x, y, 388, 152}, part == 0 ? "PART A" : "PART B", c);
      segments(kVoiceMode, c, {x + 80, y + 7, 140, 22}, std::vector<std::string>{"POLY", "MONO", "LEGATO"});
      auto &voices = glassy(put<ui::NumberBox>({x + 226, y + 7, 64, 22}, rangeOf(partId(part, kPolyphony), 1.0)), c);
      voices.setStepButtons(true);
      voices.valueText = [](double v) { return std::to_string(static_cast<int>(v + 0.5)); };
      bindings_.bind(voices, partId(part, kPolyphony));
      tips_.add(voices, "How many creatures this species can have at once: one per held note, up to 16.");
      const auto k = ui::row({x + 2, y + 34, 384, 112}, 8);
      knob(kPartTranspose, "TRANSP", c, k[0]);
      knob(kGlideTime, "GLIDE", c, k[1]);
      knob(kBendRange, "BEND", c, k[2], "The MPE bend range: how far a finger slides, in semitones.");
      knob(kPartPan, "PAN", c, k[3]);
      knob(kSkyPan, "X PAN", c, k[4], "How far a creature's place in the sky, left to right, pans its voice: "
                                        "at 100% the flock spreads across the whole stereo field.");
      knob(kPartLevel, "LEVEL", c, k[5]);
      // The output, shared by both parts.
      const int was = part_;
      part_ = -1;
      label({k[6].x, y + 10, k[6].w * 2, 16}, "OUTPUT", kDim);
      knob(kOutputWidth, "WIDTH", kDim, k[6], "The whole output's stereo width: 0% is mono, past 100% wider.");
      knob(kMasterLevel, "MASTER", kDim, k[7]);
      part_ = was;
      // Each part's card has its own copy, so they show with it.
      for (size_t i = placed_.size() - 3; i < placed_.size(); ++i)
         placed_[i].part = part;
   }

   // The route list: every route of a part, over the sky.
   // The route list over the sky: a row for each route the part uses, in route order, sixteen to a
   // page. Every route of the pool has its row built up front; layoutRoutes() places the used ones.
   void buildRoutes(int part) {
      group_ = kRoutes;
      const unsigned c = kPartColors[part];
      glass({252, 66, 696, 494}, part == 0 ? "ROUTES  PART A" : "ROUTES  PART B", kMapColor);
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
      routeInfo_ = &put<ui::Label>({430, 73, 360, 16}, "", kDim, visage::Font::kRight);
      auto &add = glassy(put<ui::Button>({800, 70, 70, 22}, "+ ADD"), kMapColor);
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
      tips_.add(add, "Add a route, then pick its source and destination. Or press MAP, touch a source and drag "
                     "any knob.");
      auto &prev = glassy(put<ui::Button>({876, 70, 30, 22}, "<"), kMapColor);
      prev.onClick = [this] {
         routePage_ = std::max(0, routePage_ - 1);
         layoutRoutes();
      };
      auto &next = glassy(put<ui::Button>({910, 70, 30, 22}, ">"), kMapColor);
      next.onClick = [this] {
         ++routePage_;
         layoutRoutes();
      };
      routeEmpty_ = &put<ui::Label>({272, 280, 656, 20}, "No routes yet. Press + ADD, or press MAP, touch a source "
                                                          "and drag any knob.", kDim);
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
         const float x = 266 + (slot / 8) * 340.0f, y = 102 + (slot % 8) * 56.0f;
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
   FlockView *view_ = nullptr;
   ui::SegmentedSwitch *partSwitch_ = nullptr;
   ui::Button *mapButton_ = nullptr;
   ui::Button *routesButton_ = nullptr;
   ui::TabBar *leftTabs_ = nullptr;
   ui::TabBar *rightTabs_ = nullptr;
   std::vector<Placed> placed_;
   std::vector<MapKnob *> mapKnobs_;
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
   int tab_ = 0;
   bool mapping_ = false;
   int armed_ = kSourceOff;
   double routeSum_ = 0.0;
   float time_ = 0.0f;
   float envLevel_[kEnvCount] = {};
   float envTime_ = 0.0f;
   int envNote_ = -1;
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
   panel.showTabs(view.leftTab, view.rightTab);
   panel.showRoutes(view.routes);
   panel.setMapping(view.mapSource != 0, view.mapSource);
   const int frames = static_cast<int>(seconds * 60.0);
   for (int i = 0; i < frames; ++i)
      panel.step(1.0f / 60.0f);
   panel.syncControls();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jsynth
