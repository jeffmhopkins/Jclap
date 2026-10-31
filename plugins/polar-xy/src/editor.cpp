// Polar XY's panel, in the MicroMonsta 2 style: a title bar, then the XY input and the polar output
// side by side, each a small night sky with its CCs beneath, and the mapping and motion settings
// below them, all on a 720 x 690 design grid scaled to the window.
//
// Both skies animate at the display's rate, about 60 frames a second, on the main thread. The input
// dot and the output orb shed ink as they move, which drifts, spreads and fades behind them like the
// flock's in JSynth, so a glide's path (straight across, or round the circle) and Inertia's swing
// past the target stay on screen for a moment. In the output sky a faint band ties the orb to where
// the controller is pointing it, stretching while Smoothing and Inertia hold it back.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/frame_pacer.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/update_notice.h>
#include <jclap/ui/xy_pad.h>

#include <visage_graphics/post_effects.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "params.h"
#include "polar.h"
#include "presets.h"

namespace polar_xy {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 690.0f;
constexpr float kHeaderHeight = 64.0f;
// Every knob gets a cell this size, so they all come out alike.
constexpr float kKnobWidth = 76.0f;
constexpr float kKnobHeight = 88.0f;

// The MicroMonsta 2 palette, and what each view's ink fades to as it ages.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kOldOrange = 0xffa8386a;
constexpr unsigned kOldBlue = 0xff5a52b0;

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

// Steady (see jclap/ui/paint.h): the skies fade these every frame.
visage::Color glow(unsigned argb, float alpha, float hdr = 1.0f) {
   return ui::steady(visage::Color(argb, hdr).withAlpha(alpha));
}

// A point in a sky's own units: -1..+1 both ways, y up.
struct Spot {
   float x = 0.0f, y = 0.0f;
};

// Where a pad position lands on the Orb, as the plugin sends it but not rounded to CC steps: a point
// on the unit disc with angle 0 to the right, growing counter-clockwise. Inside the dead zone the
// angle is undefined, so `angle` (radians) keeps what it had.
Spot orbSpot(double x, double y, const jclap::PolarSettings &s, double &angle) {
   double r = s.radiusMode == jclap::RadiusMode::FillSquare ? std::max(std::abs(x), std::abs(y)) : std::hypot(x, y);
   r = std::clamp(r, 0.0, 1.0);
   if (r < jclap::kDeadZone)
      return {};
   const double a = std::atan2(y, x);
   angle = (s.clockwise ? -a : a) + s.angleOffsetDeg * jclap::kPi / 180.0;
   r = (r - jclap::kDeadZone) / (1.0 - jclap::kDeadZone);
   return {static_cast<float>(r * std::cos(angle)), static_cast<float>(r * std::sin(angle))};
}

// The trail a moving dot leaves: puffs of ink shed along its way that drift back from where it
// went, spread and fade, joined by a fine filament while they last.
class Ink {
public:
   Ink(unsigned fresh, unsigned old, float life) : fresh_(fresh), old_(old), life_(life) {}

   // The dot is now at `to`, having moved `dt` seconds since the last call. It sheds a puff every
   // kSpacing along the way, so a fast move leaves no gaps; a dot at rest sheds nothing.
   void follow(Spot to, float dt) {
      if (!placed_) {
         at_ = to;
         placed_ = true;
         return;
      }
      const float dx = to.x - at_.x, dy = to.y - at_.y, d = std::hypot(dx, dy);
      // The dot's speed, held to kMaxSpeed: a flick shouldn't fling its ink across the sky.
      const float speed = dt > 0.0f ? d / dt : 0.0f, k = speed > kMaxSpeed ? kMaxSpeed / speed : 1.0f;
      const float vx = dt > 0.0f ? k * dx / dt : 0.0f, vy = dt > 0.0f ? k * dy / dt : 0.0f;
      // A jump (another channel, a preset) starts a new stroke instead of drawing across.
      if (d > 0.6f) {
         at_ = to;
         travel_ = 0.0f;
         broken_ = true;
         return;
      }
      for (travel_ += d; travel_ >= kSpacing && puffs_.size() < kMaxPuffs; travel_ -= kSpacing) {
         const float f = 1.0f - (travel_ - kSpacing) / std::max(d, 1e-6f);
         Puff p;
         p.x = at_.x + dx * std::clamp(f, 0.0f, 1.0f);
         p.y = at_.y + dy * std::clamp(f, 0.0f, 1.0f);
         // A little of the dot's way, backwards, and a sideways curl that varies along the way, so
         // the trail bends like ink in water.
         const float side = 0.05f * std::sin(9.0f * p.x + 7.0f * p.y);
         p.vx = -0.12f * vx - side * vy;
         p.vy = -0.12f * vy + side * vx;
         p.joined = !broken_;
         broken_ = false;
         puffs_.push_back(p);
      }
      at_ = to;
   }

   void forget() {
      placed_ = false;
      broken_ = true;
   }

   // Ages the ink `dt` seconds: it drifts and slows, and the oldest goes.
   void step(float dt) {
      const float drag = std::exp(-2.5f * dt);
      for (Puff &p : puffs_) {
         p.age += dt;
         // Kept just within the sky, so a flick's ink crackles across it instead of flying off.
         p.x = std::clamp(p.x + p.vx * dt, -kReach, kReach);
         p.y = std::clamp(p.y + p.vy * dt, -kReach, kReach);
         p.vx *= drag;
         p.vy *= drag;
      }
      while (!puffs_.empty() && puffs_.front().age >= life_)
         puffs_.pop_front();
      if (!puffs_.empty())
         puffs_.front().joined = false;
   }

   bool empty() const { return puffs_.empty(); }

   // Oldest first, so the fresh ink lies on top. `at` maps a Spot to pixels; `size` is a fresh
   // puff's radius in pixels, and `s` the panel's scale.
   template <typename At>
   void draw(visage::Canvas &c, At at, float size, float s) const {
      // Soft washes that spread as they age, added up so where the trail crosses itself it glows.
      c.setBlendMode(visage::BlendMode::Add);
      for (const Puff &p : puffs_) {
         const float t = p.age / life_, r = size * (0.7f + 2.6f * t);
         const visage::Point q = at({p.x, p.y});
         c.setColor(ui::steadyGlow(tint(t).withAlpha(0.22f * fade(p)), q, r));
         c.circle(q.x - r, q.y - r, 2 * r);
      }
      // Through them a fine filament, puff to puff, thicker and brighter toward the dot. Laid over
      // rather than added, so the joins don't bead.
      c.setBlendMode(visage::BlendMode::Alpha);
      for (size_t i = 1; i < puffs_.size(); ++i) {
         const Puff &p = puffs_[i];
         if (!p.joined)
            continue;
         const float t = p.age / life_, k = (1.0f - t) * (1.0f - t);
         const visage::Point o = at({puffs_[i - 1].x, puffs_[i - 1].y}), q = at({p.x, p.y});
         visage::Color line = tint(t * 0.8f).withAlpha(std::min(1.0f, 0.8f * fade(p)));
         line.setHdr(1.0f + 0.8f * k);
         c.setColor(ui::steady(line));
         c.segment(o.x, o.y, q.x, q.y, (0.7f + 1.5f * k) * s, true);
      }
   }

private:
   struct Puff {
      float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, age = 0.0f;
      bool joined = true; // a filament runs back to the puff before
   };

   // The ink's colour `t` of the way through its life, and how strong a puff is: it comes in
   // quickly and fades out slowly.
   visage::Color tint(float t) const {
      return visage::Color(fresh_).interpolateWith(visage::Color(old_), std::pow(t, 0.7f));
   }
   float fade(const Puff &p) const {
      return std::min(1.0f, p.age * 12.0f + 0.3f) * std::pow(1.0f - p.age / life_, 1.5f);
   }

   // Apart along the way, in sky units, and at most this many at once.
   static constexpr float kSpacing = 0.02f;
   static constexpr size_t kMaxPuffs = 900;
   // The fastest the ink is pushed by (sky units a second), and how far out it may drift.
   static constexpr float kMaxSpeed = 12.0f;
   static constexpr float kReach = 1.05f;

   unsigned fresh_, old_;
   float life_;
   std::deque<Puff> puffs_;
   Spot at_;
   float travel_ = 0.0f;
   bool placed_ = false, broken_ = true;
};

// What both skies share: a dark backdrop with a few faint motes, a bloom for what glows brighter
// than white, and the readouts along the bottom (as an XYPad has them, so both views line up).
// Nothing in it moves by itself, so a sky at rest isn't redrawn.
struct Sky {
   static constexpr int kMotes = 28;
   struct Mote {
      float x, y, b;
   };
   Mote motes[kMotes];
   visage::BloomPostEffect bloom;

   Sky() {
      // A fixed scatter (a small LCG), so the screenshot is the same every time.
      unsigned seed = 2024;
      const auto next = [&seed] {
         seed = seed * 1664525u + 1013904223u;
         return static_cast<float>(seed >> 8) / 16777216.0f;
      };
      for (Mote &m : motes)
         m = {next(), next(), next()};
      bloom.setBloomSize(14.0f);
      bloom.setBloomIntensity(0.9f);
   }

   // The sky within a view `w` x `h`, leaving the label strip below, as XYPad::pad() does.
   static visage::Bounds area(float w, float h) { return {0.0f, 0.0f, w, h - std::min(w, h) * 0.1f}; }

   void drawBackdrop(visage::Canvas &c, visage::Bounds b, unsigned tint) const {
      const float r = std::min(b.width(), b.height()) * 0.04f;
      c.setColor(visage::Brush::vertical(visage::Color(0xff111824), visage::Color(0xff090b10)));
      c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), r);
      // A faint wash of the view's colour from above, and the motes.
      c.setColor(visage::Brush::radial(glow(tint, 0.07f), glow(tint, 0.0f), visage::Point(b.x() + b.width() * 0.5f, b.y()),
                                       b.width() * 0.7f, b.height() * 0.8f));
      c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), r);
      for (const Mote &m : motes) {
         const float d = 0.6f + 0.6f * m.b;
         c.setColor(glow(0xffd8e4f0, 0.03f + 0.14f * m.b * m.b));
         c.circle(b.x() + 6.0f + m.x * (b.width() - 12.0f) - d, b.y() + 6.0f + m.y * (b.height() - 12.0f) - d, 2 * d);
      }
   }

   static void drawLabels(visage::Canvas &c, visage::Bounds b, float w, float h, float s, const std::string &left,
                          const std::string &right) {
      const float label = h - b.height();
      c.setColor(ui::defaultTheme().dimText);
      c.text(left, ui::font(13.0f * s), visage::Font::kLeft, 2.0f * s, b.bottom(), w * 0.5f, label);
      c.text(right, ui::font(13.0f * s), visage::Font::kRight, w * 0.5f - 2.0f * s, b.bottom(), w * 0.5f, label);
   }

   // A dot of light: a wide soft halo, a tighter glow and a hot core, `r` the core's radius.
   static void drawLight(visage::Canvas &c, visage::Point p, float r, unsigned color, float halo) {
      c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.22f), p, halo));
      c.circle(p.x - halo, p.y - halo, 2 * halo);
      c.setColor(ui::steadyGlow(visage::Color(color, 1.3f).withAlpha(0.7f), p, r * 2.4f));
      c.circle(p.x - r * 2.4f, p.y - r * 2.4f, r * 4.8f);
      c.setColor(glow(visage::Color(color).interpolateWith(visage::Color(0xffffffff), 0.55f).toARGB(), 1.0f, 1.6f));
      c.circle(p.x - r, p.y - r, 2 * r);
   }
};

// The XY IN pad as a sky: a faint grid, the crosshair and dot, and the dot's blue ink. It is still
// an XYPad underneath, so dragging, double-click to centre and the right-click menu all work.
class InkPad : public ui::XYPad {
public:
   InkPad(ui::ValueRange x, ui::ValueRange y) : XYPad(x, y) { setPostEffect(&sky_.bloom); }

   void setLabels(const std::string &left, const std::string &right) {
      if (left != left_ || right != right_) {
         left_ = left;
         right_ = right;
         redraw();
      }
   }

   // One frame, `dt` seconds after the last. Redraws while there is ink to fade.
   void step(float dt) {
      const bool inked = !ink_.empty();
      ink_.step(dt);
      // The ink follows the dot a touch behind, so a 7-bit controller's X and Y steps (they come
      // one axis at a time) draw a line rather than a staircase.
      const Spot to = spot();
      const float a = started_ ? 1.0f - std::exp(-dt / 0.06f) : 1.0f;
      inkAt_ = {inkAt_.x + (to.x - inkAt_.x) * a, inkAt_.y + (to.y - inkAt_.y) * a};
      started_ = true;
      ink_.follow(inkAt_, dt);
      if (inked || !ink_.empty())
         redraw();
   }

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const visage::Bounds b = Sky::area(width(), height());
      sky_.drawBackdrop(c, b, kBlue);
      c.setColor(glow(0xffffffff, 0.05f));
      for (int i = 1; i < 4; ++i) {
         c.fill(b.x() + b.width() * i / 4.0f, b.y(), 1.0f, b.height());
         c.fill(b.x(), b.y() + b.height() * i / 4.0f, b.width(), 1.0f);
      }
      c.setColor(glow(0xffffffff, 0.09f));
      c.fill(b.x() + b.width() * 0.5f, b.y(), 1.0f, b.height());
      c.fill(b.x(), b.y() + b.height() * 0.5f, b.width(), 1.0f);

      const auto at = [&](Spot p) {
         return visage::Point(b.x() + (p.x + 1.0f) * 0.5f * b.width(), b.y() + (1.0f - p.y) * 0.5f * b.height());
      };
      ink_.draw(c, at, 7.0f * s, s);

      const visage::Point p = at(spot());
      c.setColor(glow(kBlue, 0.22f));
      c.fill(p.x, b.y(), 1.0f, b.height());
      c.fill(b.x(), p.y, b.width(), 1.0f);
      Sky::drawLight(c, p, 4.0f * s, kBlue, 22.0f * s);

      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), std::min(b.width(), b.height()) * 0.04f, 1.0f);
      Sky::drawLabels(c, b, width(), height(), s, left_, right_);
   }

private:
   Spot spot() {
      return {static_cast<float>(x().range.fraction(x().value) * 2.0 - 1.0),
              static_cast<float>(y().range.fraction(y().value) * 2.0 - 1.0)};
   }

   Sky sky_;
   Ink ink_{kBlue, kOldBlue, 1.6f};
   // Where the ink is being laid, just behind the dot, from the first frame on.
   Spot inkAt_;
   bool started_ = false;
   std::string left_, right_;
};

// The polar output as a sky: the Orb's rings and spokes, angle 0 marked on the right, the orb with
// its radius line and orange ink, and the band to its target.
class OrbView : public visage::Frame {
public:
   OrbView() {
      setIgnoresMouseEvents(true, false);
      setPostEffect(&sky_.bloom);
   }

   void setLabels(const std::string &left, const std::string &right) {
      if (left != left_ || right != right_) {
         left_ = left;
         right_ = right;
         redraw();
      }
   }

   // Where the output is and is headed on the unit disc (see orbSpot); `shown` false hides it until
   // the channel has sent something.
   void set(bool shown, Spot orb, Spot target) {
      if (!shown && shown_)
         ink_.forget();
      if (shown != shown_ || orb.x != orb_.x || orb.y != orb_.y || target.x != target_.x || target.y != target_.y)
         redraw();
      shown_ = shown;
      orb_ = orb;
      target_ = target;
   }

   // One frame, `dt` seconds after the last. Redraws while there is ink to fade.
   void step(float dt) {
      const bool inked = !ink_.empty();
      ink_.step(dt);
      if (shown_)
         ink_.follow(orb_, dt);
      if (inked || !ink_.empty())
         redraw();
   }

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = ui::designScale(*this);
      const visage::Bounds b = Sky::area(width(), height());
      sky_.drawBackdrop(c, b, kOrange);

      const float cx = b.x() + b.width() * 0.5f, cy = b.y() + b.height() * 0.5f;
      const float radius = std::min(b.width(), b.height()) * 0.5f - 8.0f * s;
      const auto at = [&](Spot p) { return visage::Point(cx + p.x * radius, cy - p.y * radius); };

      // The Orb: a dim disc, its rings and spokes, a brighter rim, and angle 0's tick.
      c.setColor(visage::Brush::radial(glow(0xff1a2130, 0.9f), glow(0xff0d1118, 0.9f), visage::Point(cx, cy), radius));
      c.circle(cx - radius, cy - radius, 2 * radius);
      c.setColor(glow(0xffffffff, 0.06f));
      for (int i = 1; i < 4; ++i) {
         const float ring = radius * i / 4.0f;
         c.ring(cx - ring, cy - ring, 2 * ring, 1.0f);
      }
      for (int i = 0; i < 8; ++i) {
         const float a = i * static_cast<float>(jclap::kPi) / 4.0f;
         c.segment(cx, cy, cx + std::cos(a) * radius, cy - std::sin(a) * radius, 1.0f, false);
      }
      c.setColor(glow(0xffffffff, 0.2f));
      c.ring(cx - radius, cy - radius, 2 * radius, 1.0f);
      c.setColor(t.dimText);
      c.segment(cx + radius * 0.88f, cy, cx + radius + 5.0f * s, cy, 2.0f * s, true);

      ink_.draw(c, at, 9.0f * s, s);

      if (shown_) {
         const visage::Point p = at(orb_), q = at(target_);
         // The radius: a faint line out from the centre and the circle it sweeps.
         const float r = std::hypot(p.x - cx, p.y - cy);
         c.setColor(glow(kOrange, 0.16f));
         c.ring(cx - r, cy - r, 2 * r, 1.0f);
         c.setColor(visage::Brush::linear(glow(kOrange, 0.05f), glow(kOrange, 0.75f), visage::Point(cx, cy), p));
         c.segment(cx, cy, p.x, p.y, 1.6f * s, true);

         // The target and the band to it, thinner and brighter the further it stretches.
         const float stretch = std::hypot(q.x - p.x, q.y - p.y);
         if (stretch > 3.0f * s) {
            const float k = std::min(1.0f, stretch / (radius * 0.5f));
            c.setColor(visage::Brush::linear(glow(kBlue, 0.6f), glow(kOrange, 0.25f + 0.35f * k), q, p));
            c.segment(q.x, q.y, p.x, p.y, (1.8f - 0.9f * k) * s, true);
            const float ring = 6.0f * s;
            c.setColor(ui::steadyGlow(visage::Color(kBlue).withAlpha(0.25f), q, ring * 2.5f));
            c.circle(q.x - ring * 2.5f, q.y - ring * 2.5f, ring * 5.0f);
            c.setColor(glow(kBlue, 0.85f, 1.2f));
            c.ring(q.x - ring, q.y - ring, 2 * ring, 1.4f * s);
         }
         Sky::drawLight(c, p, 5.0f * s, kOrange, 30.0f * s);
      }

      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), std::min(b.width(), b.height()) * 0.04f, 1.0f);
      Sky::drawLabels(c, b, width(), height(), s, left_, right_);
   }

private:
   Sky sky_;
   Ink ink_{kOrange, kOldOrange, 2.2f};
   bool shown_ = false;
   Spot orb_, target_;
   std::string left_, right_;
};

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::UpdateNotice>({196, 19, 196, 26});
      auto &presets = add<ui::PresetBar>({400, 18, 308, 28}, factoryPresets(), kPresetFormat, host_);
      presets.setAccent(kOrange);
      presets.defaults = defaultValues();
      presets.currentName = [this] { return host_.presetName(); };
      presets.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };

      // Signal flow left to right: the input and its CCs, the output and its CCs; and below, what
      // happens in between.
      add<ui::Section>({12, 76, 342, 400}, "XY IN");
      add<ui::Section>({366, 76, 342, 400}, "POLAR OUT");
      add<ui::Section>({12, 488, 342, 190}, "MAPPING");
      add<ui::Section>({366, 488, 342, 190}, "MOTION");

      // Live views: what comes in on the X/Y CCs, and the angle/radius that goes out. Dragging the
      // XY pad moves the input itself, holding off the controller until the mouse lets go.
      const ui::ValueRange cc{0.0, 127.0, 64.0, 1.0};
      xy_ = &add<InkPad>({24, 116, 318, 236}, cc, cc);
      xy_->setAccent(kBlue);
      orb_ = &add<OrbView>({378, 116, 318, 236});
      // The pad sets X then Y, so a move is sent once Y has it too, never as the new X with the old
      // Y. It goes at full resolution, not rounded to CC steps, for a smooth angle and radius.
      xy_->y().onChange = [this](double) { host_.dragInput(xy_->x().value, xy_->y().value); };
      for (ui::XYPad::Axis *axis : {&xy_->x(), &xy_->y()})
         axis->onEndEdit = [this] { host_.releaseInput(); };
      add<ui::Divider>({24, 364, 318, 1});
      add<ui::Divider>({378, 364, 318, 1});

      // Each view's CCs: two knobs, and beside them a column of what else goes with them.
      constexpr float strip = 376.0f;
      knob(kParamInX, "X", kBlue, 24, strip);
      knob(kParamInY, "Y", kBlue, 100, strip);
      heading("PASS INPUT CCS", 192, strip);
      auto &pass = add<ui::Button>({192, strip + 18, 150, 24}, "PASS");
      pass.setAccent(kPurple);
      bindings_.bind(pass, kParamPassThrough);
      hint("Sends the X/Y CCs on as well", 192, strip + 52);

      knob(kParamOutAngle, "ANGLE", kOrange, 378, strip);
      knob(kParamOutRadius, "RADIUS", kOrange, 454, strip);
      // Each sends its CC alone, for a MIDI learn on the receiving end.
      heading("MIDI LEARN", 546, strip);
      const char *const sends[] = {"SEND ANGLE", "SEND RADIUS"};
      for (int i = 0; i < 2; ++i) {
         auto &send = add<ui::Button>({546.0f + i * 78.0f, strip + 18, 72, 24}, sends[i]);
         send.setAccent(kOrange);
         send.onClick = [this, i] { host_.sendOutput(i); };
      }
      hint("Sends one CC alone, to learn it", 546, strip + 52);

      // Between the two: where the angle starts and turns, and how the output follows.
      constexpr float row = 524.0f;
      knob(kParamAngleOffset, "OFFSET", kPurple, 62, row, 1.0);
      choice(kParamDirection, "DIRECTION", {"CCW", "CW"}, 192, row);
      choice(kParamRadiusMode, "RADIUS MODE", {"CIRCLE", "SQUARE"}, 192, row + 50);
      knob(kParamSmoothing, "SMOOTHING", kBlue, 378, row, 10.0);
      knob(kParamInertia, "INERTIA", kBlue, 454, row, 1.0);
      choice(kParamMotionPath, "PATH", {"STRAIGHT", "AROUND"}, 546, row);
      choice(kParamInertiaMode, "INERTIA MODE", {"PULL", "PUSH"}, 546, row + 50);
      choice(kParamResolution, "RESOLUTION", {"7-BIT", "14-BIT"}, 546, row + 100);

      // Shown when two inputs or two outputs share a CC, which the plugin won't convert.
      warning_ = &add<ui::Label>({380, 48, 328, 14}, "", kOrange, visage::Font::kRight, 0.7f);

      sync();
      startTimer(1);
   }

   // Steps at the display's rate (see FramePacer).
   void timerCallback() override {
      if (const float dt = pacer_.tick())
         step(std::min(dt, 0.05f));
   }

   // One frame, `dt` seconds after the last: what the plugin has now, and the skies moved on.
   void step(float dt) {
      sync();
      xy_->step(dt);
      orb_->step(dt);
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      canvas.setColor(t.background);
      canvas.fill(0, 0, width(), height());

      // Title bar.
      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kOrange);
      canvas.text("Polar", ui::font(28 * s), visage::Font::kLeft, 58 * s, 12 * s, 90 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("XY", ui::font(28 * s), visage::Font::kLeft, 128 * s, 12 * s, 60 * s, 40 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small orb: a ring with a radius line out to a dot.
      canvas.setColor(kOrange);
      canvas.ring(18 * s, 16 * s, 32 * s, 2.0f * s);
      canvas.segment(34 * s, 32 * s, 44 * s, 23 * s, 2.0f * s, true);
      canvas.circle(41 * s, 19 * s, 7 * s);
   }

   // A knob in a kKnobWidth x kKnobHeight cell at (x, y).
   void knob(clap_id id, const char *label, unsigned accent, float x, float y, double step = 1.0) {
      const ParamInfo &p = kParams[id];
      auto &k = add<ui::Knob>({x, y, kKnobWidth, kKnobHeight}, ui::ValueRange{p.min, p.max, p.def, step, false, p.stepped});
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
   }

   // A small title over a control, level with the knobs' labels.
   void heading(const char *label, float x, float y) {
      add<ui::Label>({x, y, 150, 14}, label, kPurple, visage::Font::kLeft);
   }

   void hint(const char *label, float x, float y) {
      add<ui::Label>({x, y, 150, 13}, label, ui::defaultTheme().dimText, visage::Font::kLeft);
   }

   // A two-way switch under its title, both options in view.
   void choice(clap_id id, const char *label, std::vector<std::string> options, float x, float y) {
      heading(label, x, y);
      auto &segments = add<ui::SegmentedSwitch>({x, y + 18, 150, 24}, std::move(options));
      segments.setAccent(kPurple);
      bindings_.bind(segments, id);
   }

   void sync() {
      int x = 64, y = 64, channel = 0;
      host_.lastInput(x, y, channel);
      // What the plugin really sent on that channel (it holds the angle at the centre, and sends
      // nothing until both axes have arrived), rather than a guess from the input.
      int angle = 0, radius = 0;
      const bool sent = host_.lastOutput(angle, radius);

      xy_->setLabels("X " + std::to_string(x), "Y " + std::to_string(y) + "  CH " + std::to_string(channel + 1));
      orb_->setLabels(sent ? "ANGLE " + std::to_string(angle) : "ANGLE -",
                      sent ? "RADIUS " + std::to_string(radius) : "RADIUS -");
      xy_->setValues(x, y);

      // The orb where the output really is, between the CC steps, and the input it is gliding to,
      // both through the same mapping as the plugin's.
      double ox, oy, tx, ty;
      const bool live = sent && host_.liveOutput(ox, oy, tx, ty);
      jclap::PolarSettings s;
      s.angleOffsetDeg = host_.paramValue(kParamAngleOffset);
      s.clockwise = std::lround(host_.paramValue(kParamDirection)) != 0;
      s.radiusMode = static_cast<jclap::RadiusMode>(std::lround(host_.paramValue(kParamRadiusMode)) != 0);
      if (live) {
         const Spot orb = orbSpot(ox, oy, s, orbAngle_);
         orb_->set(true, orb, orbSpot(tx, ty, s, targetAngle_));
      }
      else
         orb_->set(false, {}, {});

      const auto same = [this](clap_id a, clap_id b) {
         return std::lround(host_.paramValue(a)) == std::lround(host_.paramValue(b));
      };
      const auto cc = [this](clap_id id) { return static_cast<int>(std::lround(host_.paramValue(id))); };
      const bool coarse = std::lround(host_.paramValue(kParamResolution)) != 0 &&
                          !fineOutput(cc(kParamOutAngle), cc(kParamOutRadius), true);
      warning_->setText(same(kParamInX, kParamInY)               ? "X and Y share a CC: not converting"
                        : same(kParamOutAngle, kParamOutRadius) ? "Angle and radius share a CC: not converting"
                        : coarse                                ? "14-bit needs output CCs 0-31: sending 7-bit"
                                                                : "");
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::FramePacer pacer_;
   InkPad *xy_ = nullptr;
   OrbView *orb_ = nullptr;
   ui::Label *warning_ = nullptr;
   // The angles last drawn (radians), held at the centre where there is none.
   double orbAngle_ = 0.0, targetAngle_ = 0.0;
};
} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, double seconds,
                      const std::function<void(double)> &advance) {
   Panel panel(host);
   panel.stopTimer();
   const int frames = static_cast<int>(seconds * 60.0 + 0.5);
   for (int i = 0; i < frames; ++i) {
      if (advance)
         advance(1.0 / 60.0);
      panel.step(1.0f / 60.0f);
   }
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace polar_xy
