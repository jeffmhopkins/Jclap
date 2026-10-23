// JVerb's panel: the orbit view fills the window, drawn with bloom, and the controls float over it
// on translucent cards. 1200 x 780 design units, scaled to the window.
//
// The view runs at about 60 frames a second on the main thread. Each frame it takes the newest
// OrbitFrame the audio thread published (never waiting for one), sheds and moves the gas, dust
// and trails (which exist only here) and redraws. Drags become OrbitCommands for the audio thread
// and param edits for the host.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>

#include <visage_graphics/post_effects.h>
#include <visage_ui/popup_menu.h>
#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "params.h"
#include "presets.h"

namespace jverb {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1200.0f;
constexpr float kDesignHeight = 780.0f;
constexpr float kHeaderHeight = 52.0f;
// The orbit view covers everything under the header; the bodies are framed in the open space between
// the side cards, centred here (view coordinates, design units).
constexpr float kStageX = 600.0f, kStageY = 272.0f, kStageRadius = 238.0f;
// The camera's zoom (pixels per world unit) at which the sky is drawn at its own size.
constexpr float kRestZoom = 150.0f;
constexpr float kPi = 3.14159265f;

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kSpaceAccent = 0xffffb05a;
constexpr unsigned kLoopAccent = 0xffff6fd2;
constexpr unsigned kDriveAccent = 0xffee6f5c;
constexpr unsigned kEchoAccent = 0xff8fe8a8;
constexpr unsigned kText = 0xffe6e8eb;
constexpr unsigned kDim = 0xff9aa3b5;
constexpr unsigned kBodyColors[kMaxBodies] = {0xff6fe0ff, 0xffffb05a, 0xffff6fd2, 0xff8fe8a8, 0xffa98bff, 0xffff8a7a};

// Trails keep this many frames (about seven seconds) of each body's path.
constexpr int kTrailPoints = 420;
// Puffs of gas from transients.
constexpr int kMaxPuffs = 1600;
constexpr int kMaxDust = 1800;

visage::Color glow(unsigned argb, float alpha, float hdr) { return visage::Color(argb, hdr).withAlpha(alpha); }

std::string text(clap_id id, double value) {
   char buffer[48];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, kParams[id].min < 0.0};
}

// Knobs, buttons and switches over space: darker, see-through tracks.
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

// A translucent card with a title, for controls floating over space.
class Glass : public visage::Frame {
public:
   explicit Glass(std::string title = {}, unsigned accent = kDim) : title_(std::move(title)), accent_(accent) {
      setIgnoresMouseEvents(true, false);
   }
   void setTitle(std::string title, unsigned accent) {
      title_ = std::move(title);
      accent_ = accent;
      redraw();
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

// The space scene: stars, nebulae, the bodies and their trails, the dust incoming audio sheds, and
// gas from transients. Everything brighter than 1 blooms.
//
// Each transient blows a shell of soft gas out of the bodies that follow its band. The shell billows
// out and slows, drifts on a slow swirl and falls back toward the bodies. Where two bodies' clouds
// meet they push each other apart and heat up, glowing whiter along the seam.
class OrbitView : public visage::Frame {
public:
   OrbitView(EditorHost &host, float designWidth) : host_(host), designWidth_(designWidth) {
      bloom_.setBloomSize(18.0f);
      bloom_.setBloomIntensity(1.0f);
      setPostEffect(&bloom_);
      std::mt19937 rng(11);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      for (Star &s : stars_)
         s = {u(rng), u(rng), u(rng), u(rng) * 6.28f};
      for (auto &t : trails_)
         t.reserve(kTrailPoints);
      puffs_.reserve(kMaxPuffs);
      dust_.reserve(kMaxDust);
   }

   // Readout and velocity arrow for this body (-1 for none). Clicking a body selects it.
   int selected = -1;

   // One frame of animation, `dt` seconds after the last.
   void advance(const OrbitFrame &f, float dt) {
      if (f.resets != frame_.resets) {
         for (auto &t : trails_)
            t.clear();
         puffs_.clear();
         dust_.clear();
      }
      const int oldCount = frame_.count;
      frame_ = f;
      time_ += dt;
      for (int b = f.count; b < std::max(oldCount, f.count); ++b)
         trails_[b].clear();
      if (selected >= f.count)
         selected = -1;

      // Trails, in world units, so the camera can zoom without smearing them.
      for (int b = 0; b < f.count; ++b) {
         auto &t = trails_[b];
         if (t.size() == kTrailPoints)
            t.erase(t.begin());
         t.push_back({f.x[b], f.y[b]});
      }

      // The camera eases out to keep every body in frame and back in when they gather.
      float extent = 0.9f;
      for (int b = 0; b < f.count; ++b)
         extent = std::max(extent, std::hypot(f.x[b], f.y[b]));
      const float target = kStageRadius / (extent * 1.12f);
      zoom_ = zoom_ <= 0.0f ? target : zoom_ + (target - zoom_) * std::min(1.0f, dt * (target < zoom_ ? 2.5f : 0.6f));

      for (int band = 0; band < 3; ++band) {
         if (f.hits[band] != seenHits_[band]) {
            seenHits_[band] = f.hits[band];
            if (!f.frozen)
               for (int b = band; b < f.count; b += 3)
                  burst(f, b, f.hitStrength[band]);
            flare_[band] = std::max(flare_[band], f.hitStrength[band]);
         }
         flare_[band] *= std::exp(-dt * 4.0f);
      }
      if (!f.frozen) {
         moveGas(f, dt);
         moveDust(f, dt);
      }
      redraw();
   }

   void draw(visage::Canvas &c) override {
      const float s = width() / designWidth_;
      c.setColor(0xff04050a);
      c.fill(0, 0, width(), height());
      drawBackdrop(c, s);

      const float ppu = zoom_ * s;
      const float cx = kStageX * s, cy = kStageY * s;
      const auto X = [&](float x) { return cx + x * ppu; };
      const auto Y = [&](float y) { return cy + y * ppu; };
      const OrbitFrame &f = frame_;

      // The centre of mass, and a faint tether between each body and the next.
      c.setColor(glow(kText, 0.3f, 1.0f));
      c.segment(cx - 7 * s, cy, cx + 7 * s, cy, 1.0f * s, true);
      c.segment(cx, cy - 7 * s, cx, cy + 7 * s, 1.0f * s, true);
      for (int b = 0; b < f.count && f.count > 1; ++b) {
         const int n = (b + 1) % f.count;
         c.setColor(glow(kText, 0.06f, 1.0f));
         c.segment(X(f.x[b]), Y(f.y[b]), X(f.x[n]), Y(f.y[n]), 1.0f * s, true);
      }

      // Gas, oldest first: soft discs that fade in, swell and thin out; hot ones go white.
      for (const Puff &p : puffs_) {
         const float t = p.age / p.life;
         // Thinner where many puffs pile up, so a dense knot stays a cloud rather than a white blob.
         const float alpha = std::min(1.0f, p.age * 5.0f) * (1.0f - t) * (1.0f - t) * (0.17f + 0.03f * p.heat) /
                             std::sqrt(std::max(1.0f, p.crowd / 5.0f));
         const float r = p.size * ppu;
         visage::Color tint = visage::Color(kBodyColors[p.body]).interpolateWith(0xffffffff, std::min(0.25f, 0.15f * p.heat));
         tint.setHdr(1.0f + 0.3f * p.heat);
         c.setColor(visage::Brush::radial(tint.withAlpha(alpha), tint.withAlpha(0.0f), visage::Point(X(p.x), Y(p.y)), r));
         c.circle(X(p.x) - r, Y(p.y) - r, 2 * r);
      }

      // Trails: faint and thin at the tail, bright at the body.
      for (int b = 0; b < f.count; ++b) {
         const auto &t = trails_[b];
         const size_t n = t.size();
         for (size_t k = 1; k < n; ++k) {
            const float a = static_cast<float>(k) / n;
            c.setColor(glow(kBodyColors[b], 0.04f + 0.86f * a * a, 1.0f + 0.7f * a * a * a));
            c.segment(X(t[k - 1].x), Y(t[k - 1].y), X(t[k].x), Y(t[k].y), (0.7f + 2.6f * a) * s, true);
         }
      }

      // Dust: short streaks along its motion.
      for (const Dust &d : dust_) {
         const float life = 1.0f - d.age / d.life;
         const float streak = 0.035f;
         c.setColor(glow(kBodyColors[d.body], 0.2f + 0.75f * life, 1.1f + 1.0f * life));
         c.segment(X(d.x - d.vx * streak), Y(d.y - d.vy * streak), X(d.x), Y(d.y), 1.7f * s, true);
      }

      // Bodies: a soft halo, a bright core, a white-hot centre; louder bands glow harder.
      for (int b = 0; b < f.count; ++b) {
         const float r = (4.5f + 3.0f * std::sqrt(f.mass[b])) * s;
         const float energy = std::min(1.0f, f.band[b % 3]) * 0.6f + flare_[b % 3];
         const float bx = X(f.x[b]), by = Y(f.y[b]);
         const float halo = r * (3.5f + 1.5f * energy);
         c.setColor(visage::Brush::radial(glow(kBodyColors[b], 0.24f + 0.14f * energy, 1.0f),
                                          glow(kBodyColors[b], 0.0f, 1.0f), visage::Point(bx, by), halo));
         c.circle(bx - halo, by - halo, 2 * halo);
         c.setColor(glow(kBodyColors[b], 1.0f, 1.6f + 0.5f * energy));
         c.circle(bx - r, by - r, 2 * r);
         c.setColor(glow(0xffffffff, 1.0f, 1.8f + 0.5f * energy));
         c.circle(bx - 0.4f * r, by - 0.4f * r, 0.8f * r);
      }

      const int shown = selected >= 0 ? selected : hover_;
      if (shown >= 0 && shown < f.count)
         drawReadout(c, s, shown, X(f.x[shown]), Y(f.y[shown]), ppu);

      if (!f.running) {
         c.setColor(glow(kDim, 0.8f, 1.0f));
         c.text("PAUSED", ui::font(12 * s), visage::Font::kCenter, cx - 100 * s, cy + kStageRadius * s, 200 * s, 18 * s);
      }
      c.setColor(glow(kDim, 0.7f, 1.0f));
      c.text("drag a body to move it  ·  drag its arrow for velocity  ·  scroll on it for mass  ·  "
             "double-click to add a body, right-click to remove one",
             ui::font(11 * s), visage::Font::kCenter, 240 * s, 534 * s, 720 * s, 16 * s);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const visage::Point p = e.position;
      if (e.shouldTriggerPopup()) {
         if (bodyAt(p) >= 0 && frame_.count > 2)
            setBodies(frame_.count - 1);
         return;
      }
      if (e.repeatClickCount() >= 2 && bodyAt(p) < 0) {
         if (frame_.count < kMaxBodies) {
            const visage::Point w = toWorld(p);
            host_.send({OrbitCommand::Kind::Spawn, 0, w.x, w.y});
            setBodies(frame_.count + 1);
         }
         return;
      }
      if (selected >= 0 && arrowHit(selected, p)) {
         dragArrow_ = true;
         return;
      }
      dragBody_ = bodyAt(p);
      selected = dragBody_;
      redraw();
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      const visage::Point w = toWorld(e.position);
      if (dragArrow_ && selected >= 0) {
         const float k = 1.0f / kArrowScale;
         host_.send({OrbitCommand::Kind::Velocity, static_cast<int8_t>(selected), (w.x - frame_.x[selected]) * k,
                     (w.y - frame_.y[selected]) * k});
      }
      else if (dragBody_ >= 0)
         host_.send({OrbitCommand::Kind::Place, static_cast<int8_t>(dragBody_), w.x, w.y});
   }

   void mouseUp(const visage::MouseEvent &) override {
      dragBody_ = -1;
      dragArrow_ = false;
   }

   void mouseMove(const visage::MouseEvent &e) override {
      const int h = bodyAt(e.position);
      if (h != hover_) {
         hover_ = h;
         redraw();
      }
   }

   void mouseExit(const visage::MouseEvent &) override { hover_ = -1; }

   // Scrolling on a body changes its mass, about 5% a notch.
   bool mouseWheel(const visage::MouseEvent &e) override {
      const int b = bodyAt(e.position);
      if (b < 0)
         return false;
      const clap_id id = bodyParam(b, kBody1Mass);
      const double m = std::clamp(host_.paramValue(id) * std::exp(0.05 * (e.precise_wheel_delta_y != 0.0f
                                                                              ? e.precise_wheel_delta_y
                                                                              : e.wheel_delta_y)),
                                  kParams[id].min, kParams[id].max);
      host_.beginEdit(id);
      host_.setParam(id, m);
      host_.endEdit(id);
      return true;
   }

private:
   struct Puff {
      float x, y, vx, vy, age, life, size, heat, crowd;
      int body;
   };
   struct Star {
      float x, y, b, phase;
   };
   struct Dust {
      float x, y, vx, vy, age, life;
      int body;
   };
   struct Point2 {
      float x, y;
   };

   static constexpr float kArrowScale = 0.45f;

   void setBodies(int count) {
      host_.beginEdit(kBodies);
      host_.setParam(kBodies, count);
      host_.endEdit(kBodies);
   }

   float ppu() const { return zoom_ * width() / designWidth_; }
   visage::Point toWorld(visage::Point p) const {
      const float s = width() / designWidth_;
      return {(p.x - kStageX * s) / ppu(), (p.y - kStageY * s) / ppu()};
   }
   visage::Point toScreen(float x, float y) const {
      const float s = width() / designWidth_;
      return {kStageX * s + x * ppu(), kStageY * s + y * ppu()};
   }

   int bodyAt(visage::Point p) const {
      const float s = width() / designWidth_;
      int best = -1;
      float bestD = 16.0f * s;
      for (int b = 0; b < frame_.count; ++b) {
         const visage::Point q = toScreen(frame_.x[b], frame_.y[b]);
         const float d = std::hypot(q.x - p.x, q.y - p.y);
         if (d < bestD)
            bestD = d, best = b;
      }
      return best;
   }

   bool arrowHit(int b, visage::Point p) const {
      const visage::Point tip = toScreen(frame_.x[b] + frame_.vx[b] * kArrowScale, frame_.y[b] + frame_.vy[b] * kArrowScale);
      return std::hypot(tip.x - p.x, tip.y - p.y) < 10.0f * width() / designWidth_;
   }

   // A transient: a shell of gas blown out of body `b`, faster and thicker the harder it hit.
   void burst(const OrbitFrame &f, int b, float strength) {
      const int n = 14 + static_cast<int>(26.0f * strength);
      const float life = std::clamp(0.5f * f.decay, 1.8f, 4.5f), speed = 0.7f + 1.5f * strength;
      const float turn = uniform_(rng_) * 2.0f * kPi;
      for (int i = 0; i < n && static_cast<int>(puffs_.size()) < kMaxPuffs; ++i) {
         const float a = turn + 2.0f * kPi * (i + 0.6f * uniform_(rng_)) / n, v = speed * (0.75f + 0.5f * uniform_(rng_));
         puffs_.push_back({f.x[b] + 0.03f * std::cos(a), f.y[b] + 0.03f * std::sin(a),
                           0.6f * f.vx[b] + v * std::cos(a), 0.6f * f.vy[b] + v * std::sin(a), 0.0f,
                           life * (0.7f + 0.5f * uniform_(rng_)), (0.06f + 0.05f * strength) * (0.8f + 0.4f * uniform_(rng_)),
                           0.0f, 1.0f, b});
      }
   }

   void moveGas(const OrbitFrame &f, float dt) {
      // Sort the puffs into a coarse grid, so each one only looks at its neighbours.
      constexpr float kCell = 0.2f;
      constexpr int kGrid = 40;
      for (auto &cell : grid_)
         cell.clear();
      const auto cellOf = [&](float v) { return std::clamp(static_cast<int>(std::floor(v / kCell)) + kGrid / 2, 0, kGrid - 1); };
      for (int i = 0; i < static_cast<int>(puffs_.size()); ++i)
         grid_[cellOf(puffs_[i].y) * kGrid + cellOf(puffs_[i].x)].push_back(i);

      const float h = dt * 0.8f;
      for (int i = 0; i < static_cast<int>(puffs_.size()); ++i) {
         Puff &p = puffs_[i];
         // A slow swirl that turns over time, drag that stalls the shell as it billows out, and a
         // weak pull from every body.
         float ax = 0.3f * std::sin(1.7f * p.y + 0.37f * time_ + p.body) - 2.0f * p.vx;
         float ay = 0.3f * std::cos(1.9f * p.x - 0.29f * time_ - p.body) - 2.0f * p.vy;
         for (int b = 0; b < f.count; ++b) {
            const float dx = f.x[b] - p.x, dy = f.y[b] - p.y;
            const float r2 = dx * dx + dy * dy + 0.12f;
            const float g = 0.04f * f.mass[b] / (r2 * std::sqrt(r2));
            ax += dx * g;
            ay += dy * g;
         }
         // Another body's gas pushes back where it overlaps this one, and the seam heats up.
         float contact = 0.0f, crowd = 0.0f;
         const int cx = cellOf(p.x), cy = cellOf(p.y);
         for (int gy = std::max(0, cy - 1); gy <= std::min(kGrid - 1, cy + 1); ++gy)
            for (int gx = std::max(0, cx - 1); gx <= std::min(kGrid - 1, cx + 1); ++gx)
               for (int j : grid_[gy * kGrid + gx]) {
                  const Puff &q = puffs_[j];
                  const float dx = p.x - q.x, dy = p.y - q.y, reach = 0.6f * (p.size + q.size);
                  const float d2 = dx * dx + dy * dy;
                  if (d2 >= reach * reach)
                     continue;
                  crowd += 1.0f;
                  if (q.body == p.body)
                     continue;
                  const float d = std::sqrt(d2) + 1e-4f, overlap = 1.0f - d / reach;
                  ax += 0.6f * overlap * dx / d;
                  ay += 0.6f * overlap * dy / d;
                  contact += overlap;
               }
         p.vx += h * ax;
         p.vy += h * ay;
         p.heat += (std::min(2.0f, contact) - p.heat) * std::min(1.0f, dt * 3.0f);
         p.crowd = crowd;
      }
      for (Puff &p : puffs_) {
         p.x += h * p.vx;
         p.y += h * p.vy;
         p.age += dt;
         p.size += dt * 0.12f;
      }
      puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff &p) { return p.age > p.life; }),
                   puffs_.end());
   }

   void moveDust(const OrbitFrame &f, float dt) {
      // Each band's level sheds dust from the bodies that follow it.
      for (int band = 0; band < 3; ++band) {
         const float rate = 160.0f * std::min(1.5f, f.band[band]);
         spawnCarry_[band] += rate * dt;
         while (spawnCarry_[band] >= 1.0f) {
            spawnCarry_[band] -= 1.0f;
            for (int b = band; b < f.count; b += 3) {
               if (static_cast<int>(dust_.size()) >= kMaxDust)
                  break;
               const float a = uniform_(rng_) * 2.0f * kPi, sp = 0.15f + 0.45f * uniform_(rng_);
               dust_.push_back({f.x[b], f.y[b], f.vx[b] * 0.55f + sp * std::cos(a), f.vy[b] * 0.55f + sp * std::sin(a),
                                0.0f, std::clamp(0.6f * f.decay, 0.8f, 6.0f), b});
            }
         }
      }
      // Dust falls toward every body (in sim time, which runs at about 0.8 units a second) with drag
      // that follows the decay, and is swallowed when it reaches one.
      const float h = dt * 0.8f;
      const float drag = 0.6f + 2.0f / std::max(0.5f, f.decay);
      for (Dust &d : dust_) {
         float ax = -drag * d.vx, ay = -drag * d.vy;
         for (int b = 0; b < f.count; ++b) {
            const float dx = f.x[b] - d.x, dy = f.y[b] - d.y;
            const float r2 = dx * dx + dy * dy + 0.004f;
            const float g = 0.35f * f.mass[b] / (r2 * std::sqrt(r2));
            ax += dx * g;
            ay += dy * g;
         }
         d.vx += h * ax;
         d.vy += h * ay;
         d.x += h * d.vx;
         d.y += h * d.vy;
         d.age += dt;
      }
      dust_.erase(std::remove_if(dust_.begin(), dust_.end(),
                                 [&](const Dust &d) {
                                    if (d.age > d.life)
                                       return true;
                                    for (int b = 0; b < f.count; ++b)
                                       if (d.age > 0.25f && std::hypot(f.x[b] - d.x, f.y[b] - d.y) < 0.025f)
                                          return true;
                                    return false;
                                 }),
                  dust_.end());
   }

   // The sky follows the camera: zooming in spreads the stars and swells the nebulae, zooming out
   // packs them closer. Near stars (the brighter ones) move more than far ones, so the sky has
   // depth. The star field tiles, so zooming out never leaves an empty edge.
   void drawBackdrop(visage::Canvas &c, float s) {
      const float w = width(), h = height();
      const float cx = kStageX * s, cy = kStageY * s;
      const float zoom = zoom_ > 0.0f ? zoom_ / kRestZoom : 1.0f;
      // Nebulae, far away, and brightening a little with the tank.
      const float lift = std::min(1.0f, frame_.late * 3.0f);
      const float nk = std::clamp(std::pow(zoom, 0.25f), 0.8f, 1.4f);
      const auto nebula = [&](float x, float y, float r, unsigned color, float a) {
         const float nx = cx + (x * s - cx) * nk, ny = cy + (y * s - cy) * nk, nr = r * s * nk;
         c.setColor(visage::Brush::radial(glow(color, a * (1.0f + 0.5f * lift), 1.0f), glow(color, 0.0f, 1.0f),
                                          visage::Point(nx, ny), nr));
         c.circle(nx - nr, ny - nr, 2 * nr);
      };
      nebula(300, 170, 420, 0xff4a2a8a, 0.2f);
      nebula(930, 470, 460, 0xff123f6e, 0.24f);
      nebula(760, 70, 300, 0xff7a2a5a, 0.11f);
      nebula(160, 590, 360, 0xff1b5560, 0.15f);
      for (const Star &st : stars_) {
         const float k = std::pow(zoom, 0.35f + 0.65f * st.b);
         const float twinkle = 0.85f + 0.15f * std::sin(time_ * 1.3f + st.phase);
         const float r = (0.6f + st.b * st.b * 1.8f) * s * std::clamp(std::sqrt(k), 0.7f, 1.4f);
         c.setColor(glow(st.b > 0.93f ? 0xffcfe0ff : 0xffffffff, (0.2f + 0.75f * st.b) * twinkle,
                         st.b > 0.96f ? 1.5f : 1.0f));
         // Every copy of this star's tile that lands on screen.
         const float ox = st.x * w - cx, oy = st.y * h - cy;
         for (int ty = static_cast<int>(std::floor((-cy / k - oy) / h)); ty <= static_cast<int>(std::floor(((h - cy) / k - oy) / h)); ++ty)
            for (int tx = static_cast<int>(std::floor((-cx / k - ox) / w)); tx <= static_cast<int>(std::floor(((w - cx) / k - ox) / w)); ++tx)
               c.circle(cx + (ox + tx * w) * k - r, cy + (oy + ty * h) * k - r, 2 * r);
      }
   }

   void drawReadout(visage::Canvas &c, float s, int b, float bx, float by, float ppu) {
      const OrbitFrame &f = frame_;
      c.setColor(glow(kText, 0.85f, 1.0f));
      c.ring(bx - 16 * s, by - 16 * s, 32 * s, 1.2f * s);
      if (b == selected) {
         // The velocity arrow, with a handle at the tip to drag.
         const float ex = bx + f.vx[b] * kArrowScale * ppu, ey = by + f.vy[b] * kArrowScale * ppu;
         const float ang = std::atan2(ey - by, ex - bx), head = 10 * s;
         c.setColor(glow(kBodyColors[b], 0.95f, 1.2f));
         c.segment(bx, by, ex, ey, 2.0f * s, true);
         c.triangle(ex + std::cos(ang) * 4 * s, ey + std::sin(ang) * 4 * s, ex - std::cos(ang - 0.45f) * head,
                    ey - std::sin(ang - 0.45f) * head, ex - std::cos(ang + 0.45f) * head,
                    ey - std::sin(ang + 0.45f) * head);
         c.setColor(glow(kText, 1.0f, 1.0f));
         c.circle(ex - 5 * s, ey - 5 * s, 10 * s);
      }
      char line1[64], line2[96];
      std::snprintf(line1, sizeof(line1), "%s   m %.2f   v %.2f", kBodyNames[b], f.mass[b], std::hypot(f.vx[b], f.vy[b]));
      std::snprintf(line2, sizeof(line2), "r %.2f  ->  delay %.0f ms   pan %s%.0f", std::hypot(f.x[b], f.y[b]),
                    f.delayMs[b], f.x[b] < 0 ? "L" : "R", std::min(100.0f, std::fabs(f.x[b]) / 1.3f * 100.0f));
      const float tw = 196 * s, th = 42 * s;
      const float tx = std::min(bx + 22 * s, width() - tw - 250 * s), ty = by + 14 * s;
      c.setColor(visage::Color(0xd00b0e16));
      c.roundedRectangle(tx, ty, tw, th, 6 * s);
      c.setColor(glow(kBodyColors[b], 0.8f, 1.0f));
      c.roundedRectangleBorder(tx, ty, tw, th, 6 * s, 1.0f * s);
      c.setColor(kBodyColors[b]);
      c.text(line1, ui::font(12 * s), visage::Font::kLeft, tx + 10 * s, ty + 4 * s, tw - 14 * s, 16 * s);
      c.setColor(kDim);
      c.text(line2, ui::font(11 * s), visage::Font::kLeft, tx + 10 * s, ty + 21 * s, tw - 14 * s, 16 * s);
   }

   EditorHost &host_;
   float designWidth_;
   visage::BloomPostEffect bloom_;
   OrbitFrame frame_;
   Star stars_[420];
   std::vector<Point2> trails_[kMaxBodies];
   std::vector<Puff> puffs_;
   std::vector<int> grid_[40 * 40];
   std::vector<Dust> dust_;
   uint32_t seenHits_[3] = {};
   float flare_[3] = {};
   float spawnCarry_[3] = {};
   float zoom_ = 0.0f;
   float time_ = 0.0f;
   int hover_ = -1, dragBody_ = -1;
   bool dragArrow_ = false;
   std::mt19937 rng_{7};
   std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
};

// The preset browser in the header: arrows step through every preset, the name opens a menu of them
// by category, and SAVE names the current settings and writes them to the user's preset folder.
class PresetBar : public visage::Frame {
public:
   explicit PresetBar(EditorHost &host) : host_(host) { rescan(); }

   void draw(visage::Canvas &c) override {
      const float s = height() / 28.0f, w = width(), h = height();
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
      c.setColor(hover_ == Zone::Save ? kText : kOrange);
      c.text("SAVE", ui::font(11 * s), visage::Font::kCenter, w - save, 0, save, h);
      if (editor_ && editor_->isVisible())
         return;
      const std::string name = host_.presetName();
      const Preset *p = find(name);
      c.setColor(hover_ == Zone::Name ? 0xffffffff : kText);
      c.text(name.empty() ? std::string("Untitled") : name, ui::font(13 * s), visage::Font::kCenter, a, 0,
             w - 2 * a - save, h);
      if (p) {
         c.setColor(kDim);
         c.text(p->category, ui::font(9 * s), visage::Font::kLeft, a + 8 * s, 0, 80 * s, h);
      }
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
      step(e.wheel_delta_y > 0 ? -1 : 1);
      return true;
   }

private:
   enum class Zone { None, Prev, Name, Next, Save };
   static constexpr float kArrow = 26.0f, kSave = 52.0f;
   static constexpr int kSaveId = 1 << 20, kUserBase = 1 << 16;

   Zone zoneAt(float x) const {
      const float s = height() / 28.0f, w = width();
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

   void rescan() { user_ = userPresets(); }

   // Every preset in menu order: factory first, then the user's.
   int total() const { return static_cast<int>(factoryPresets().size() + user_.size()); }
   const Preset &at(int i) const {
      const auto &f = factoryPresets();
      return i < static_cast<int>(f.size()) ? f[i] : user_[i - f.size()];
   }
   const Preset *find(const std::string &name) const {
      for (int i = 0; i < total(); ++i)
         if (at(i).name == name)
            return &at(i);
      return nullptr;
   }

   void load(int i) {
      const Preset &p = at(i);
      applyPreset(p, host_);
      host_.setPresetName(p.name);
      // Restart the orbits from the preset's masses and velocities.
      host_.send({OrbitCommand::Kind::Reset, 0, 0.0f, 0.0f});
      redraw();
   }

   void step(int dir) {
      rescan();
      const int n = total();
      int current = -1;
      for (int i = 0; i < n; ++i)
         if (at(i).name == host_.presetName())
            current = i;
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
      menu.show(this, {kArrow * height() / 28.0f, height()});
   }

   void startSave() {
      if (!editor_) {
         editor_ = std::make_unique<visage::TextEditor>();
         editor_->setJustification(visage::Font::kCenter);
         editor_->onEnterKey() += [this] { finishSave(true); };
         editor_->onEscapeKey() += [this] { finishSave(false); };
         addChild(editor_.get(), false);
      }
      const float s = height() / 28.0f;
      editor_->setBounds(kArrow * s, 3 * s, width() - (2 * kArrow + kSave) * s, height() - 6 * s);
      editor_->setFont(ui::font(13 * s));
      editor_->setText(host_.presetName().empty() ? std::string("My Preset") : host_.presetName());
      editor_->setVisible(true);
      editor_->requestKeyboardFocus();
      editor_->selectAll();
      redraw();
   }

   void finishSave(bool apply) {
      if (!editor_ || !editor_->isVisible())
         return;
      const std::string name = presetFileName(editor_->text().toUtf8());
      editor_->setVisible(false);
      if (apply && writePreset(userPresetFolder() / (name + kPresetExtension), host_)) {
         host_.setPresetName(name);
         rescan();
      }
      redraw();
   }

   EditorHost &host_;
   std::vector<Preset> user_;
   std::unique_ptr<visage::TextEditor> editor_;
   Zone hover_ = Zone::None;
};

// A card's "+" slot that adds a body.
class AddSlot : public visage::Frame {
public:
   std::function<void()> onClick;
   int count = 3;
   void draw(visage::Canvas &c) override {
      const float s = height() / 152.0f;
      c.setColor(visage::Color(hover_ ? 0xc0141a26 : 0x900b0e17));
      c.roundedRectangle(0, 0, width(), height(), 10 * s);
      c.setColor(visage::Color(0x2effffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * s, 1.0f * s);
      c.setColor(hover_ ? kText : kDim);
      c.text("+", ui::font(34 * s), visage::Font::kCenter, 0, 36 * s, width(), 40 * s);
      c.text("ADD BODY", ui::font(11 * s), visage::Font::kCenter, 0, 86 * s, width(), 16 * s);
      char line[32];
      std::snprintf(line, sizeof(line), "%d of %d · denser", count, kMaxBodies);
      c.setColor(kDim);
      c.text(line, ui::font(10 * s), visage::Font::kCenter, 0, 104 * s, width(), 16 * s);
   }
   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &) override {
      if (onClick)
         onClick();
   }

private:
   bool hover_ = false;
};

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      view_ = &add<OrbitView>({0, kHeaderHeight, kDesignWidth, kDesignHeight - kHeaderHeight}, host, kDesignWidth);
      buildHeader();
      buildSpace();
      buildLoop();
      buildBodies();
      buildOutput();
      layoutBodies(currentBodies());
      last_ = std::chrono::steady_clock::now();
      startTimer(16);
   }

   // One animation frame; the timer calls it with the real time since the last one.
   void step(float dt) {
      OrbitFrame f;
      if (host_.readFrame(f))
         frame_ = f;
      view_->advance(frame_, dt);
      const int count = currentBodies();
      if (count != laidOut_)
         layoutBodies(count);
      float l, r;
      host_.outputPeaks(l, r);
      meters_[0]->setLevel(l);
      meters_[1]->setLevel(r);
   }

   void timerCallback() override {
      const auto now = std::chrono::steady_clock::now();
      const float dt = std::chrono::duration<float>(now - last_).count();
      last_ = now;
      step(std::clamp(dt, 0.0f, 0.05f));
   }

   OrbitView &view() { return *view_; }

   void draw(visage::Canvas &c) override {
      const float s = scale();
      c.setColor(0xff070910);
      c.fill(0, 0, width(), kHeaderHeight * s);
      c.setColor(visage::Color(0x22ffffff));
      c.fill(0, (kHeaderHeight - 1) * s, width(), 1 * s);
      // Logo: three bodies on a figure-eight.
      c.setColor(visage::Color(0x88ffffff));
      c.ring(13 * s, 18 * s, 16 * s, 1.2f * s);
      c.ring(27 * s, 18 * s, 16 * s, 1.2f * s);
      c.setColor(kBodyColors[0]);
      c.circle(10 * s, 23 * s, 7 * s);
      c.setColor(kBodyColors[1]);
      c.circle(24.5f * s, 22.5f * s, 7 * s);
      c.setColor(kBodyColors[2]);
      c.circle(39 * s, 23 * s, 7 * s);
      c.setColor(kOrange);
      c.text("J", ui::font(24 * s), visage::Font::kLeft, 54 * s, 6 * s, 30 * s, 40 * s);
      c.setColor(kText);
      c.text("Verb", ui::font(24 * s), visage::Font::kLeft, 67 * s, 6 * s, 90 * s, 40 * s);
      c.setColor(kDim);
      c.text("three-body reverb", ui::font(12 * s), visage::Font::kLeft, 140 * s, 6 * s, 200 * s, 40 * s);
   }

private:
   int currentBodies() const { return std::clamp(static_cast<int>(host_.paramValue(kBodies) + 0.5), 2, kMaxBodies); }

   ui::Knob &knob(clap_id id, const char *label, unsigned accent, ui::Rect cell, const char *tip = nullptr) {
      auto &k = add<ui::Knob>(cell, rangeOf(id));
      k.setTheme(glassTheme());
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      if (tip)
         tips_.add(k, tip);
      return k;
   }

   Glass &glass(ui::Rect r, const char *title, unsigned accent) {
      auto &g = add<Glass>(r, title, accent);
      g.designHeight = r.h;
      return g;
   }

   template <typename T>
   T &glassy(T &widget, unsigned accent) {
      widget.setTheme(glassTheme());
      widget.setAccent(accent);
      return widget;
   }

   void buildHeader() {
      add<PresetBar>({380, 12, 340, 28}, host_);
      auto &run = glassy(add<ui::Button>({760, 14, 60, 24}, "RUN"), kOrange);
      bindings_.bind(run, kRun);
      tips_.add(run, "Pause or run the orbits. Paused, the reverb keeps the shape it had.");
      auto &reset = glassy(add<ui::Button>({828, 14, 60, 24}, "RESET"), kOrange);
      reset.onClick = [this] { host_.send({OrbitCommand::Kind::Reset, 0, 0.0f, 0.0f}); };
      tips_.add(reset, "Put the bodies back where they start.");
      auto &clock = glassy(add<ui::SegmentedSwitch>({896, 14, 186, 24}, std::vector<std::string>{"FREE", "SYNC", "RETRIG"}),
                           kOrange);
      bindings_.bind(clock, kClock);
      tips_.add(clock, "FREE runs on its own. SYNC fits one lap of the start orbit into BARS and restarts when "
                       "the host plays; RETRIG only restarts.");
      auto &bars = glassy(add<ui::NumberBox>({1090, 14, 94, 24}, rangeOf(kSyncBars, 1.0)), kOrange);
      bars.setStepButtons(true);
      bars.valueText = [](double v) { return std::to_string(static_cast<int>(v + 0.5)) + " bar"; };
      bindings_.bind(bars, kSyncBars);
      tips_.add(bars, "Bars per lap when synced.");
   }

   // Left: the space itself, on two pages.
   void buildSpace() {
      const unsigned a = kSpaceAccent;
      glass({16, 64, 214, 536}, "", a);
      auto &tabs = glassy(add<ui::TabBar>({24, 70, 198, 26}, std::vector<std::string>{"SPACE", "SHAPE"}), a);
      tabs.addToPage(0, knob(kDecay, "DECAY", a, {26, 104, 194, 122}, "How long the tail lasts (RT60)."));
      const auto g = ui::grid({26, 232, 194, 360}, 2, 3);
      tabs.addToPage(0, knob(kSize, "SYSTEM SIZE", a, g[0], "Scales the whole system: every delay line, and the room."));
      tabs.addToPage(0, knob(kGravity, "GRAVITY", a, g[1], "How hard the bodies pull: higher runs the orbits faster and tighter."));
      tabs.addToPage(0, add<ui::Label>(ui::Rect{g[2].x, g[2].y + 2, g[2].w, 16}, "BODIES", kText, visage::Font::kCenter));
      auto &bodies = glassy(add<ui::NumberBox>({g[2].x + 4, g[2].y + 34, g[2].w - 8, 28}, rangeOf(kBodies, 1.0)), a);
      bodies.setStepButtons(true);
      bodies.valueText = [](double v) { return std::to_string(static_cast<int>(v + 0.5)); };
      bindings_.bind(bodies, kBodies);
      tabs.addToPage(0, bodies);
      tips_.add(bodies, "Each body owns two delay lines: fewer is sparse and echoey, more is dense.");
      tabs.addToPage(0, add<ui::Label>(ui::Rect{g[2].x, g[2].y + 72, g[2].w, 16}, "sparse - dense", kDim,
                                       visage::Font::kCenter));
      tabs.addToPage(0, knob(kMotion, "MOTION", a, g[3], "How far each body's distance stretches its delay lines, "
                                                         "and so how much Doppler you hear."));
      tabs.addToPage(0, knob(kPredelay, "PREDELAY", a, g[4]));
      tabs.addToPage(0, knob(kDampHigh, "DAMP HIGH", a, g[5], "Highs above this die faster on every pass."));

      const auto h = ui::grid({26, 104, 194, 488}, 2, 4);
      tabs.addToPage(1, knob(kAttack, "ATTACK", a, h[0], "Right of centre the tail swells in; left of centre it "
                                                         "plays in reverse windows."));
      tabs.addToPage(1, knob(kDiffusion, "DIFFUSION", a, h[1], "How much the input is smeared before the tank."));
      tabs.addToPage(1, knob(kEarlyLate, "EARLY/LATE", a, h[2]));
      tabs.addToPage(1, knob(kBassMult, "BASS MULT", a, h[3], "Bass decay relative to the mids."));
      tabs.addToPage(1, knob(kBassXover, "BASS XOVER", a, h[4]));
      tabs.addToPage(1, knob(kLowCut, "LOW CUT", a, h[5], "Filters the input before the reverb."));
      tabs.addToPage(1, knob(kHighCut, "HIGH CUT", a, h[6], "Filters the input before the reverb."));
      tabs.addToPage(1, knob(kReact, "REACT", a, h[7], "How hard transients in the input kick the bodies."));
      tabs.select(0);
   }

   // Right: the outer loop and the drive, on two pages.
   void buildLoop() {
      const unsigned a = kLoopAccent;
      glass({970, 64, 214, 536}, "", a);
      auto &tabs = glassy(add<ui::TabBar>({978, 70, 198, 26}, std::vector<std::string>{"LOOP", "DRIVE", "ECHO"}), a);
      auto &freeze = glassy(add<ui::Button>({984, 106, 186, 26}, "FREEZE"), a);
      bindings_.bind(freeze, kFreeze);
      tips_.add(freeze, "Holds the tail forever and lets nothing new in. The dust hangs where it is.");
      tabs.addToPage(0, freeze);
      const auto g = ui::grid({980, 140, 194, 330}, 2, 3);
      tabs.addToPage(0, knob(kFeedback, "FEEDBACK", a, g[0], "Feeds the tank's output back into its input through "
                                                             "the loop. Past 100% it grows into the limiter."));
      tabs.addToPage(0, knob(kShimmer, "SHIMMER", a, g[1], "How much of the loop is pitch shifted."));
      tabs.addToPage(0, add<ui::Label>(ui::Rect{g[2].x, g[2].y + 2, g[2].w, 16}, "PITCH", kText, visage::Font::kCenter));
      auto &pitch = glassy(add<ui::NumberBox>({g[2].x + 4, g[2].y + 34, g[2].w - 8, 28}, rangeOf(kShimmerPitch, 1.0)), a);
      pitch.setStepButtons(true);
      pitch.valueText = [](double v) { return text(kShimmerPitch, v); };
      pitch.parseText = [](const std::string &t, double &v) { return parseParam(kShimmerPitch, t.c_str(), v); };
      bindings_.bind(pitch, kShimmerPitch);
      tabs.addToPage(0, pitch);
      tabs.addToPage(0, knob(kTilt, "TILT", a, g[3], "Darkens or brightens what goes round the loop."));
      tabs.addToPage(0, knob(kSmear, "SMEAR", a, g[4], "Allpass smearing in the loop."));
      tabs.addToPage(0, add<ui::Label>(ui::Rect{g[5].x, g[5].y + 2, g[5].w, 16}, "SAFETY", kText, visage::Font::kCenter));
      auto &safety = glassy(add<ui::SegmentedSwitch>({g[5].x + 4, g[5].y + 34, g[5].w - 8, 28},
                                                     std::vector<std::string>{"SOFT", "HARD"}),
                            a);
      bindings_.bind(safety, kSafety);
      tabs.addToPage(0, safety);
      tips_.add(safety, "How the loop is kept from running away: a soft saturator or a hard clip.");

      const unsigned d = kDriveAccent;
      auto &on = glassy(add<ui::Button>({984, 106, 186, 26}, "DRIVE ON"), d);
      bindings_.bind(on, kDriveOn);
      tabs.addToPage(1, on);
      auto &where = glassy(add<ui::SegmentedSwitch>({984, 142, 186, 26}, std::vector<std::string>{"PRE", "LOOP", "POST"}), d);
      bindings_.bind(where, kDriveWhere);
      tabs.addToPage(1, where);
      tips_.add(where, "PRE drives the input, LOOP drives every pass of the feedback loop, POST drives the reverb's "
                       "output.");
      std::vector<std::string> types;
      for (const char *t : kTypeNames)
         types.emplace_back(t);
      auto &type = glassy(add<ui::Dropdown>({984, 176, 186, 26}, types), d);
      bindings_.bind(type, kDriveType);
      tabs.addToPage(1, type);
      const auto h = ui::grid({980, 210, 194, 380}, 2, 3);
      tabs.addToPage(1, knob(kDrive, "DRIVE", d, h[0]));
      tabs.addToPage(1, knob(kDriveTone, "TONE", d, h[1], "Tilts what goes into the shaper, and back after."));
      tabs.addToPage(1, knob(kDriveBias, "BIAS", d, h[2], "Asymmetry for even harmonics; for CRUSH, the hold."));
      tabs.addToPage(1, knob(kDriveMix, "MIX", d, h[3]));
      tabs.addToPage(1, knob(kDriveTrim, "TRIM", d, h[4]));

      const unsigned e = kEchoAccent;
      auto &echoOn = glassy(add<ui::Button>({984, 106, 186, 26}, "ECHO ON"), e);
      bindings_.bind(echoOn, kEchoOn);
      tabs.addToPage(2, echoOn);
      tips_.add(echoOn, "A long delay, up to four seconds, free or synced to the host's tempo.");
      auto &slot = glassy(add<ui::SegmentedSwitch>({984, 142, 186, 26}, std::vector<std::string>{"INTO", "LOOP", "AFTER"}), e);
      bindings_.bind(slot, kEchoWhere);
      tabs.addToPage(2, slot);
      tips_.add(slot, "INTO: the echoes feed the reverb, so every repeat blooms. LOOP: the tail comes back around "
                      "through the tank in long passes. AFTER: echoes of the reverb itself.");
      std::vector<std::string> syncs(std::begin(kEchoSyncNames), std::end(kEchoSyncNames));
      auto &sync = glassy(add<ui::Dropdown>({984, 176, 118, 26}, syncs), e);
      bindings_.bind(sync, kEchoSync);
      tabs.addToPage(2, sync);
      tips_.add(sync, "Free uses TIME in milliseconds; a note value follows the host's tempo.");
      auto &pingPong = glassy(add<ui::Button>({1108, 176, 62, 26}, "PING"), e);
      bindings_.bind(pingPong, kEchoPingPong);
      tabs.addToPage(2, pingPong);
      tips_.add(pingPong, "Ping-pong: repeats bounce between left and right.");
      const auto q = ui::grid({980, 212, 194, 300}, 2, 2);
      auto &time = knob(kEchoTime, "TIME", e, q[0], "The echo's time when SYNC is Free. A body routed to Echo "
                                                     "bends it like a tape machine.");
      time.valueText = [this](double v) {
         const int sync = static_cast<int>(host_.paramValue(kEchoSync) + 0.5);
         return sync > 0 && sync < kEchoSyncCount ? std::string(kEchoSyncNames[sync]) : text(kEchoTime, v);
      };
      tabs.addToPage(2, time);
      tabs.addToPage(2, knob(kEchoFeedback, "FEEDBACK", e, q[1], "How many repeats. Past 100% they saturate "
                                                                  "instead of running away."));
      tabs.addToPage(2, knob(kEchoTone, "TONE", e, q[2], "Darker or brighter with every repeat."));
      tabs.addToPage(2, knob(kEchoMix, "MIX", e, q[3]));
      tabs.select(0);
   }

   // Bottom: a card per body, laid out for however many there are, and a slot to add one.
   void buildBodies() {
      std::vector<std::string> targets;
      for (const char *t : kTargetNames)
         targets.emplace_back(t);
      for (int b = 0; b < kMaxBodies; ++b) {
         Card &card = cards_[b];
         card.glass = &glass({0, 0, 10, kCardHeight}, kBodyNames[b], kBodyColors[b]);
         card.target = &glassy(add<ui::Dropdown>({0, 0, 10, 10}, targets), kBodyColors[b]);
         bindings_.bind(*card.target, bodyParam(b, kBody1Target));
         tips_.add(*card.target, "What this body's speed and close passes move, besides its own delay lines.");
         card.mass = &knob(bodyParam(b, kBody1Mass), "MASS", kBodyColors[b], {0, 0, 10, 10});
         card.velocity = &knob(bodyParam(b, kBody1Velocity), "VELOCITY", kBodyColors[b], {0, 0, 10, 10});
         card.amount = &knob(bodyParam(b, kBody1Amount), "AMOUNT", kBodyColors[b], {0, 0, 10, 10});
      }
      addSlot_ = &add<AddSlot>({0, 0, 10, kCardHeight});
      addSlot_->onClick = [this] {
         const int n = currentBodies();
         if (n < kMaxBodies) {
            host_.beginEdit(kBodies);
            host_.setParam(kBodies, n + 1);
            host_.endEdit(kBodies);
         }
      };
   }

   void layoutBodies(int count) {
      laidOut_ = count;
      const float x0 = 16, right = 846, gap = 10, y = kCardTop;
      const float slot = count < kMaxBodies ? 96.0f : 0.0f;
      const float w = (right - x0 - slot - (slot > 0 ? gap : 0) - gap * (count - 1)) / count;
      const bool narrow = w < 170;
      for (int b = 0; b < kMaxBodies; ++b) {
         Card &card = cards_[b];
         const bool on = b < count;
         for (visage::Frame *f : {static_cast<visage::Frame *>(card.glass), static_cast<visage::Frame *>(card.target),
                                  static_cast<visage::Frame *>(card.mass), static_cast<visage::Frame *>(card.velocity),
                                  static_cast<visage::Frame *>(card.amount)})
            f->setVisible(on);
         if (!on)
            continue;
         const float x = x0 + b * (w + gap);
         move(*card.glass, {x, y, w, kCardHeight});
         const float dw = narrow ? w - 16 : std::min(120.0f, w - 104);
         move(*card.target, {x + w - dw - 8, narrow ? y + 28 : y + 6, dw, 22});
         const auto cells = ui::row({x + 4, narrow ? y + 54 : y + 34, w - 8, narrow ? 94.0f : 112.0f}, 3);
         card.velocity->setLabel(narrow ? "SPEED" : "VELOCITY");
         card.amount->setLabel(narrow ? "AMT" : "AMOUNT");
         move(*card.mass, cells[0]);
         move(*card.velocity, cells[1]);
         move(*card.amount, cells[2]);
      }
      addSlot_->setVisible(count < kMaxBodies);
      addSlot_->count = count;
      if (count < kMaxBodies)
         move(*addSlot_, {right - slot, y, slot, kCardHeight});
      addSlot_->redraw();
   }

   void buildOutput() {
      glass({858, kCardTop, 326, kCardHeight}, "OUTPUT", kOrange);
      const auto c = ui::row({862, kCardTop + 30, 284, 116}, 4);
      knob(kWidth, "WIDTH", kOrange, c[0]);
      knob(kDuck, "DUCK", kOrange, c[1], "Turns the reverb down while the input plays.");
      knob(kMix, "MIX", kOrange, c[2]);
      knob(kLevel, "LEVEL", kOrange, c[3]);
      for (int i = 0; i < 2; ++i) {
         meters_[i] = &add<ui::Meter>({1150.0f + 14 * i, kCardTop + 14, 10, kCardHeight - 28});
         meters_[i]->setTheme(glassTheme());
      }
   }

   static constexpr float kCardTop = 612.0f, kCardHeight = 152.0f;

   struct Card {
      Glass *glass = nullptr;
      ui::Dropdown *target = nullptr;
      ui::Knob *mass = nullptr, *velocity = nullptr, *amount = nullptr;
   };

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   OrbitView *view_ = nullptr;
   Card cards_[kMaxBodies];
   AddSlot *addSlot_ = nullptr;
   ui::Meter *meters_[2] = {};
   OrbitFrame frame_;
   int laidOut_ = 0;
   std::chrono::steady_clock::time_point last_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, double seconds, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.stopTimer();
   const int frames = static_cast<int>(seconds * 60.0);
   for (int i = 0; i < frames; ++i)
      panel.step(1.0f / 60.0f);
   panel.view().selected = 1;
   panel.step(1.0f / 60.0f);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jverb
