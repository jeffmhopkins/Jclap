// JBreath's panel, in JSynth's look: a header with the preset bar, then on the left the EXCITER
// (which instrument, its valve, and how it's blown), the BORE (its resonances, with the partials of
// the note as it's played) and the PLAYER (tongue and vibrato); on the right the breath loops, each
// voice drawn as a glowing delay-embedding loop of its own sound, and the VOICE card (mode, where
// the breath comes from, the press curve and the output). 1648 x 940 design units, scaled to the
// window.

#include "editor.h"

#include <jclap/ui/design_frame.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "params.h"
#include "presets.h"
#include "voice.h"

namespace jbreath {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1648.0f;
constexpr float kDesignHeight = 940.0f;
constexpr float kHeaderHeight = 52.0f;
constexpr float kPiF = 3.14159265f;

constexpr unsigned kText = 0xffe6e8eb;
constexpr unsigned kDim = 0xff9aa3b5;
constexpr unsigned kFaint = 0xff5d6577;
constexpr unsigned kCard = 0xff1a1e25;
constexpr unsigned kCardBorder = 0xff2a303a;
constexpr unsigned kWell = 0xff111419;
constexpr unsigned kPress = 0xffff6fd2;

// Each instrument's family colour: the jet cyan, the single reeds orange, the double reed violet,
// the lips coral.
constexpr unsigned kModelColors[kModelCount] = {0xff6fe0ff, 0xfff28c28, 0xfff2a54a, 0xffa98bff, 0xffff7a59};
unsigned modelColor(int model) { return kModelColors[std::clamp(model, 0, kModelCount - 1)]; }

const char *const kFamilyNames[kModelCount] = {"JET", "REED", "REED", "DOUBLE REED", "LIPS"};
const char *const kModelSubtitles[kModelCount] = {"jet  ·  open pipe", "single reed  ·  cylinder", "single reed  ·  cone",
                                                  "double reed  ·  narrow cone", "lips  ·  flared horn"};

visage::Color glow(unsigned argb, float alpha, float hdr = 1.0f) {
   return ui::steady(visage::Color(argb, hdr).withAlpha(alpha));
}

std::string text(clap_id id, double value) {
   char buffer[64];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id) {
   const auto &p = kParams[id];
   const bool stepped = (p.flags & CLAP_PARAM_IS_STEPPED) != 0;
   return {p.min, p.max, p.defaultValue, stepped ? 1.0 : 0.0, p.min < 0.0, stepped};
}

int modelOf(const EditorHost &host) { return std::clamp(static_cast<int>(host.paramValue(kModel) + 0.5), 0, kModelCount - 1); }

// A frame drawn in design units: s() turns one into pixels.
class Drawn : public visage::Frame {
public:
   explicit Drawn(float designWidth) : designWidth_(designWidth) {}

protected:
   float s() const { return width() / designWidth_; }
   // A dark rounded well, the backdrop of every graph.
   void well(visage::Canvas &c) {
      const float k = s();
      c.setColor(kWell);
      c.roundedRectangle(0, 0, width(), height(), 8 * k);
      c.setColor(visage::Color(0x14ffffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 8 * k, 1.0f * k);
   }
   void label(visage::Canvas &c, const std::string &t, float x, float y, float w, unsigned color, float size = 11.0f,
              visage::Font::Justification j = visage::Font::kLeft) {
      const float k = s();
      c.setColor(color);
      c.text(t, ui::font(size * k), j, x * k, y * k, w * k, (size + 6) * k);
   }

private:
   float designWidth_;
};

// ------------------------------------------------------------------ exciter

// The five instruments, one row each. Clicking one picks it.
class InstrumentList : public Drawn {
public:
   InstrumentList(float w, EditorHost &host) : Drawn(w), host_(host) {}

   void draw(visage::Canvas &c) override {
      const float k = s(), rowH = rowHeight();
      const int selected = modelOf(host_);
      for (int i = 0; i < kModelCount; ++i) {
         const float y = i * (rowH + kGap);
         const unsigned color = modelColor(i);
         const bool on = i == selected;
         c.setColor(on ? glow(color, 0.16f) : visage::Color(i == hover_ ? 0xff262c35 : 0xff1f242c));
         c.roundedRectangle(0, y * k, width(), rowH * k, 6 * k);
         c.setColor(on ? glow(color, 0.85f, 1.2f) : visage::Color(0x18ffffff));
         c.roundedRectangleBorder(0, y * k, width(), rowH * k, 6 * k, (on ? 1.5f : 1.0f) * k);
         label(c, kModelNames[i], 12, y + 5, 140, on ? color : kText, 15.0f);
         label(c, kModelSubtitles[i], 12, y + 25, 150, kDim, 10.5f);
         drawIcon(c, i, (designW() - 26) * k, (y + rowH / 2) * k, k, on ? color : kDim);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const int i = rowAt(e.position.y);
      if (i < 0)
         return;
      host_.beginEdit(kModel);
      host_.setParam(kModel, i);
      host_.endEdit(kModel);
      redraw();
   }
   void mouseMove(const visage::MouseEvent &e) override {
      const int i = rowAt(e.position.y);
      if (i != hover_) {
         hover_ = i;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = -1;
      redraw();
   }

private:
   static constexpr float kGap = 6.0f;
   float designW() const { return width() / s(); }
   float rowHeight() const { return (height() / s() - kGap * (kModelCount - 1)) / kModelCount; }
   int rowAt(float y) const {
      const int i = static_cast<int>(y / s() / (rowHeight() + kGap));
      return i >= 0 && i < kModelCount ? i : -1;
   }

   // A small glyph of how the instrument makes its sound.
   static void drawIcon(visage::Canvas &c, int model, float x, float y, float k, unsigned color) {
      c.setColor(color);
      const float w = 1.6f * k;
      switch (model) {
      case kFlute: // the jet, split by the edge
         c.segment(x - 12 * k, y, x + 8 * k, y, w, true);
         c.segment(x + 3 * k, y - 5 * k, x + 9 * k, y, w, true);
         c.segment(x + 3 * k, y + 5 * k, x + 9 * k, y, w, true);
         break;
      case kClarinet: // a reed on a straight tube
         c.segment(x - 12 * k, y + 6 * k, x + 10 * k, y + 6 * k, w, true);
         c.segment(x - 12 * k, y + 6 * k, x + 10 * k, y - 4 * k, w, true);
         break;
      case kSax: // a reed on a widening cone
         c.segment(x - 12 * k, y + 2 * k, x + 10 * k, y + 8 * k, w, true);
         c.segment(x - 12 * k, y + 2 * k, x + 10 * k, y - 6 * k, w, true);
         break;
      case kOboe: // two blades
         c.segment(x - 12 * k, y - 6 * k, x + 10 * k, y - 1 * k, w, true);
         c.segment(x - 12 * k, y + 6 * k, x + 10 * k, y + 1 * k, w, true);
         break;
      default: // lips
         c.segment(x - 11 * k, y - 4 * k, x + 9 * k, y - 4 * k, w, true);
         c.segment(x - 11 * k, y + 4 * k, x + 9 * k, y + 4 * k, w, true);
         c.segment(x - 11 * k, y - 4 * k, x - 11 * k, y + 4 * k, w, true);
         c.segment(x + 9 * k, y - 4 * k, x + 9 * k, y + 4 * k, w, true);
         break;
      }
   }

   EditorHost &host_;
   int hover_ = -1;
};

// The exciter's valve: how the air through it answers the pressure across it, with a dot where
// each sounding voice is blowing.
class ValveGraph : public Drawn {
public:
   explicit ValveGraph(float w) : Drawn(w) {}

   void set(int model, double embouchure, std::vector<float> breaths) {
      model_ = model;
      embouchure_ = embouchure;
      breaths_ = std::move(breaths);
      redraw();
   }

   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const float x0 = 14, x1 = W - 14, y0 = 34, y1 = H - 26;
      const unsigned color = modelColor(model_);
      // Grid.
      c.setColor(visage::Color(0x0effffff));
      for (int i = 1; i < 4; ++i) {
         c.fill((x0 + (x1 - x0) * i / 4) * k, y0 * k, 1 * k, (y1 - y0) * k);
         c.fill(x0 * k, (y0 + (y1 - y0) * i / 4) * k, (x1 - x0) * k, 1 * k);
      }
      label(c, yName(), 12, 8, W - 24, kDim, 10.5f);
      label(c, xName(), 12, H - 20, W - 24, kDim, 10.5f);

      // The curve, filled beneath.
      constexpr int n = 120;
      visage::Path area, line;
      const auto px = [&](double x) { return static_cast<float>((x0 + (x1 - x0) * x / xMax()) * k); };
      const auto py = [&](double y) { return static_cast<float>((y1 - (y1 - y0) * y) * k); };
      area.moveTo(px(0), py(0));
      for (int i = 0; i <= n; ++i) {
         const double x = xMax() * i / n;
         area.lineTo(px(x), py(curve(x)));
         if (i == 0)
            line.moveTo(px(x), py(curve(x)));
         else
            line.lineTo(px(x), py(curve(x)));
      }
      area.lineTo(px(xMax()), py(0));
      area.close();
      c.setColor(visage::Brush::vertical(glow(color, 0.35f), glow(color, 0.02f)));
      c.fill(area);
      c.setColor(glow(color, 0.95f, 1.3f));
      c.fill(line.stroke(2.0f * k));

      // Where the valve shuts (reeds) or the jet overblows.
      if (const double shut = shutAt(); shut > 0.0 && shut < xMax()) {
         c.setColor(visage::Color(0x30ffffff));
         for (float y = y0; y < y1; y += 8)
            c.fill(px(shut), y * k, 1 * k, 4 * k);
         label(c, shutName(), (px(shut) / k) - 110, y0 - 2, 104, kDim, 10.0f, visage::Font::kRight);
      }

      // Each voice's operating point.
      for (size_t i = 0; i < breaths_.size(); ++i) {
         const double x = std::clamp(static_cast<double>(operating(breaths_[i])), 0.0, xMax());
         const float cx = px(x), cy = py(curve(x));
         c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.5f), {cx, cy}, 14 * k));
         c.circle(cx - 14 * k, cy - 14 * k, 28 * k);
         c.setColor(glow(0xffffffff, 0.95f));
         c.circle(cx - 4 * k, cy - 4 * k, 8 * k);
         if (breaths_.size() == 1)
            label(c, "this note", cx / k - 40, cy / k - 26, 80, kText, 10.5f, visage::Font::kCenter);
      }
   }

private:
   const char *yName() const { return model_ == kFlute ? "JET SPEED" : model_ == kTrumpet ? "AIR THROUGH THE LIPS" : "AIR FLOW"; }
   const char *xName() const {
      switch (model_) {
      case kFlute:
         return "BREATH  →";
      case kTrumpet:
         return "MOUTH PRESSURE  →";
      default:
         return "PRESSURE ACROSS THE REED  →";
      }
   }
   const char *shutName() const { return model_ == kFlute ? "OVERBLOWS" : "REED SHUTS"; }
   double xMax() const { return model_ == kFlute || model_ == kTrumpet ? 1.3 : 1.25; }
   // Where the curve turns over: the reed closes, or the jet is fast enough to jump a register.
   double shutAt() const {
      switch (model_) {
      case kFlute:
         return 1.05;
      case kClarinet:
         return 1.0;
      case kSax:
      case kOboe:
         return 1.0;
      default:
         return -1.0;
      }
   }
   double curve(double x) const {
      switch (model_) {
      case kFlute: // jet speed grows as the square root of the pressure
         return std::sqrt(x / 1.3) * 0.92;
      case kTrumpet: // the lips open with the pressure, and the flow through them as its root: x^1.5
         return 0.92 * std::pow(x / 1.3, 1.5);
      default: { // a reed valve: u = (1 - W) sqrt(W), shut at W = 1; normalised to its peak
         const double w = std::min(x, 1.0);
         const double soft = model_ == kClarinet ? 1.0 : model_ == kSax ? 0.85 + 0.3 * (1 - embouchure_) : 0.7;
         return 0.95 * (1 - w) * std::sqrt(w) / 0.3849 * (model_ == kClarinet ? 1.0 : soft);
      }
      }
   }
   // A voice's breath, as a point on the x axis.
   double operating(float breath) const {
      return model_ == kClarinet ? 0.7 * breath : model_ == kFlute || model_ == kTrumpet ? breath : 0.75 * breath;
   }

   int model_ = kClarinet;
   double embouchure_ = 0.5;
   std::vector<float> breaths_;
};

// ------------------------------------------------------------------ bore

// The bore's input impedance, from the same loop the model runs (its length, reflection and losses),
// with the partials of the loudest sounding note as they're really coming out.
class BoreGraph : public Drawn {
public:
   explicit BoreGraph(float w) : Drawn(w) {}

   void set(int model, double hz, bool playing, std::vector<float> partials) {
      model_ = model;
      hz_ = hz;
      playing_ = playing;
      partials_ = std::move(partials);
      redraw();
   }

   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const float x0 = 12, x1 = W - 12, y0 = 44, y1 = H - 14;
      const unsigned color = modelColor(model_);
      const auto px = [&](double f) {
         return static_cast<float>((x0 + (x1 - x0) * std::log(f / kLow) / std::log(kHigh / kLow)) * k);
      };
      // Decades.
      for (double f : {100.0, 1000.0, 5000.0}) {
         c.setColor(visage::Color(0x12ffffff));
         c.fill(px(f), y0 * k, 1 * k, (y1 - y0) * k);
         label(c, f >= 1000 ? std::to_string(static_cast<int>(f / 1000)) + "k" : "100", px(f) / k + 4, y0 - 2, 40, kFaint,
               10.0f);
      }
      label(c, title(), W - 470, 8, 456, kText, 11.5f, visage::Font::kRight);
      char sub[96];
      std::snprintf(sub, sizeof(sub), playing_ ? "●  partials of %s as played" : "●  play a note to see its partials (%s shown)",
                    ui::noteName(static_cast<int>(std::lround(69 + 12 * std::log2(hz_ / 440.0)))).c_str());
      label(c, sub, W - 470, 24, 456, kDim, 10.5f, visage::Font::kRight);

      // |Z| in dB, the top 36 dB of it.
      constexpr int n = 260;
      std::array<double, n + 1> db{};
      double top = -1e9;
      for (int i = 0; i <= n; ++i) {
         const double f = kLow * std::pow(kHigh / kLow, static_cast<double>(i) / n);
         db[i] = 20.0 * std::log10(std::abs(impedance(f)) + 1e-9);
         top = std::max(top, db[i]);
      }
      const auto py = [&](double d) { return static_cast<float>((y1 - (y1 - y0) * std::clamp((d - top + 36) / 36, 0.0, 1.0)) * k); };
      visage::Path area, line;
      for (int i = 0; i <= n; ++i) {
         const double f = kLow * std::pow(kHigh / kLow, static_cast<double>(i) / n);
         if (i == 0) {
            area.moveTo(px(f), y1 * k);
            line.moveTo(px(f), py(db[i]));
         } else {
            line.lineTo(px(f), py(db[i]));
         }
         area.lineTo(px(f), py(db[i]));
      }
      area.lineTo(px(kHigh), y1 * k);
      area.close();
      c.setColor(visage::Brush::vertical(glow(color, 0.32f), glow(color, 0.03f)));
      c.fill(area);
      c.setColor(glow(color, 0.9f, 1.2f));
      c.fill(line.stroke(1.6f * k));

      // The note's partials: a stem for each harmonic, as tall as it sounds (or, with nothing
      // playing, where the bore puts it).
      const double loudest = partials_.empty() ? 0.0 : *std::max_element(partials_.begin(), partials_.end());
      for (int h = 1; h <= kPartials; ++h) {
         const double f = hz_ * h;
         if (f > kHigh * 0.98)
            break;
         double level;
         if (playing_ && loudest > 0.0)
            level = 20.0 * std::log10(partials_[h - 1] / loudest + 1e-6) + top; // the loudest at the top
         else
            level = 20.0 * std::log10(std::abs(impedance(f)) + 1e-9);
         if (level < top - 36)
            continue;
         const float x = px(f), y = py(level);
         c.setColor(glow(0xffffffff, playing_ ? 0.55f : 0.22f));
         c.fill(x - 0.75f * k, y, 1.5f * k, y1 * k - y);
         c.setColor(glow(0xffffffff, playing_ ? 0.95f : 0.4f));
         c.circle(x - 3 * k, y - 3 * k, 6 * k);
      }
   }

   static constexpr int kPartials = 16;

private:
   static constexpr double kLow = 60.0, kHigh = 10000.0, kFs = 96000.0;

   static std::complex<double> onePole(double f, double cut) {
      const double a = std::exp(-2 * kPi * std::min(cut, 0.45 * kFs) / kFs);
      return (1 - a) / (1.0 - a * std::polar(1.0, -2 * kPi * f / kFs));
   }
   // The bore seen from the exciter: (1 + R e^-jwT) / (1 - R e^-jwT), with the model's own loop.
   std::complex<double> impedance(double f) const {
      std::complex<double> r;
      double period = 1.0 / hz_;
      switch (model_) {
      case kFlute:
         r = 0.97 * onePole(f, 3.2 * hz_);
         break;
      case kClarinet: {
         const double cut = std::max(1300.0, 2.2 * hz_);
         r = -0.96 * onePole(f, cut) * onePole(f, cut);
         period = 0.5 / hz_;
         break;
      }
      case kSax:
         r = 0.95 * onePole(f, 5200);
         break;
      case kOboe:
         r = 0.95 * onePole(f, 3800);
         break;
      default:
         r = 0.985 * (onePole(f, 8 * hz_) - 2.0 * onePole(f, 20));
         break;
      }
      const std::complex<double> e = r * std::polar(1.0, -2 * kPi * f * period);
      return (1.0 + e) / (1.0 - e);
   }
   const char *title() const {
      switch (model_) {
      case kFlute:
         return "OPEN PIPE: RESONANCES ON EVERY HARMONIC";
      case kClarinet:
         return "CLOSED CYLINDER: RESONANCES ON ODD HARMONICS";
      case kSax:
         return "CONE: EVERY HARMONIC, OVERBLOWS AT THE OCTAVE";
      case kOboe:
         return "NARROW CONE: EVERY HARMONIC, A DARKER LOOP";
      default:
         return "FLARED HORN: HARMONIC RESONANCES, NO PEDAL";
      }
   }

   int model_ = kClarinet;
   double hz_ = 293.66;
   bool playing_ = false;
   std::vector<float> partials_;
};

// The bore's shape, as a row of tags with this instrument's lit.
class BoreShape : public Drawn {
public:
   explicit BoreShape(float w) : Drawn(w) {}
   void setModel(int model) {
      model_ = model;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      static const char *const names[4] = {"OPEN PIPE", "CYLINDER", "CONE", "FLARE"};
      const int lit = model_ == kFlute ? 0 : model_ == kClarinet ? 1 : model_ == kTrumpet ? 3 : 2;
      const float k = s(), w = width() / k / 4;
      const unsigned color = modelColor(model_);
      for (int i = 0; i < 4; ++i) {
         const bool on = i == lit;
         c.setColor(on ? glow(color, 0.9f) : visage::Color(0xff1f242c));
         c.roundedRectangle((i * w + 1) * k, 0, (w - 2) * k, height(), 4 * k);
         label(c, names[i], i * w, height() / k / 2 - 9, w, on ? 0xff15181d : kDim, 11.5f, visage::Font::kCenter);
      }
   }

private:
   int model_ = kClarinet;
};

// How long the tube is for the note sounding (or D4): a real instrument's length, from the speed of
// sound and how the bore resonates (half a wavelength, or a quarter for the closed cylinder).
class BoreInfo : public Drawn {
public:
   explicit BoreInfo(float w) : Drawn(w) {}
   void set(int model, double hz, bool playing) {
      if (model == model_ && std::fabs(hz - hz_) < 0.05 && playing == playing_)
         return;
      model_ = model;
      hz_ = hz;
      playing_ = playing;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k;
      const double length = 343.0 / (model_ == kClarinet ? 4.0 : 2.0) / hz_;
      const int note = static_cast<int>(std::lround(69 + 12 * std::log2(hz_ / 440.0)));
      char big[32], small[96], third[96];
      std::snprintf(big, sizeof(big), "%.2f m", length);
      std::snprintf(small, sizeof(small), "TUBE FOR %s  ·  %.1f Hz%s", ui::noteName(note).c_str(), hz_, playing_ ? "" : "  ·  NOT PLAYING");
      std::snprintf(third, sizeof(third), "%s", model_ == kClarinet ? "A QUARTER WAVE: CLOSED AT THE REED, OPEN AT THE BELL"
                                                : model_ == kTrumpet ? "HALF A WAVE: THE BELL'S FLARE PUTS EVERY MODE ON A HARMONIC"
                                                                     : "HALF A WAVE: OPEN, OR A CONE FROM ITS TIP");
      c.setColor(modelColor(model_));
      c.text(big, ui::font(24 * k), visage::Font::kLeft, 16 * k, 10 * k, 140 * k, 34 * k);
      label(c, small, 140, 16, W - 150, kText, 11.0f);
      label(c, third, 16, 52, W - 28, kDim, 10.0f);
   }

private:
   int model_ = -1;
   double hz_ = 0.0;
   bool playing_ = false;
};

// ------------------------------------------------------------------ player

// A note's breath over time: the tongue lets the air go with a kick, the breath settles, and dies
// away with the release.
class TongueGraph : public Drawn {
public:
   explicit TongueGraph(float w) : Drawn(w) {}
   void set(double attackMs, double kick, double releaseMs, unsigned color) {
      attack_ = attackMs;
      kick_ = kick;
      release_ = releaseMs;
      color_ = color;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      label(c, "TONGUE", 12, 8, 100, kText, 11.5f);
      char t[64];
      std::snprintf(t, sizeof(t), "%.0f ms  ·  kick %.0f%%", attack_, kick_ * 100);
      label(c, t, W - 170, 8, 158, kDim, 10.5f, visage::Font::kRight);
      const float x0 = 12, x1 = W - 12, y0 = 34, y1 = H - 12;
      // 0..hold s held, then the release; time on a square-root axis so short attacks show.
      const double hold = 0.6, total = hold + std::max(0.15, release_ / 1000.0 * 1.5);
      const auto px = [&](double t) { return static_cast<float>((x0 + (x1 - x0) * std::sqrt(t / total)) * k); };
      const auto py = [&](double v) { return static_cast<float>((y1 - (y1 - y0) * v / 1.5) * k); };
      visage::Path area, line;
      area.moveTo(px(0), py(0));
      constexpr int n = 160;
      double b = 0.0, prev = 0.0;
      const double up = 1.0 - std::exp(-4.0 / (attack_ / 1000.0 * n / total * 1000.0 / 1000.0 + 1e-9));
      (void)up;
      for (int i = 0; i <= n; ++i) {
         const double tt = total * i / n * (static_cast<double>(i) / n); // denser near the start
         const double dt = tt - prev;
         prev = tt;
         if (tt < hold) {
            const double target = 1.0 + kick_ * std::exp(-tt / 0.04);
            b += (target - b) * (1.0 - std::exp(-4.0 * dt / (attack_ / 1000.0)));
         } else {
            b *= std::exp(-4.0 * dt / (release_ / 1000.0));
         }
         area.lineTo(px(tt), py(b));
         if (i == 0)
            line.moveTo(px(tt), py(b));
         else
            line.lineTo(px(tt), py(b));
      }
      area.lineTo(px(total), py(0));
      area.close();
      c.setColor(visage::Brush::vertical(glow(color_, 0.32f), glow(color_, 0.03f)));
      c.fill(area);
      c.setColor(glow(color_, 0.95f, 1.2f));
      c.fill(line.stroke(1.8f * k));
      // Handles: the end of the attack, and where the release starts.
      for (double tt : {attack_ / 1000.0, hold}) {
         c.setColor(glow(0xffffffff, 0.9f));
         const float x = px(tt), y = tt < hold ? py(1.0 + kick_ * std::exp(-tt / 0.04) * 0.9) : py(1.0);
         c.ring(x - 4 * k, y - 4 * k, 8 * k, 1.6f * k);
      }
   }

private:
   double attack_ = 25, kick_ = 0.25, release_ = 90;
   unsigned color_ = 0xfff28c28;
};

// Vibrato: the breath's swell (and the pitch's, dashed) growing in after the delay.
class VibratoGraph : public Drawn {
public:
   explicit VibratoGraph(float w) : Drawn(w) {}
   void set(double rate, double depth, double pitch, double delayMs, unsigned color) {
      rate_ = rate;
      depth_ = depth;
      pitch_ = pitch;
      delay_ = delayMs;
      color_ = color;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      label(c, "VIBRATO", 12, 8, 100, kText, 11.5f);
      char t[64];
      std::snprintf(t, sizeof(t), "%.2f Hz  ·  %.0f%%  ·  %.0f ct", rate_, depth_ * 100, pitch_);
      label(c, t, W - 200, 8, 188, kDim, 10.5f, visage::Font::kRight);
      const float x0 = 12, x1 = W - 12, yc = (34 + H - 12) / 2, amp = (H - 12 - 34) / 2;
      const double total = 2.0;
      c.setColor(visage::Color(0x14ffffff));
      c.fill(x0 * k, yc * k, (x1 - x0) * k, 1 * k);
      const float dx = (x0 + (x1 - x0) * std::min(1.0, delay_ / 1000.0 / total)) * k;
      c.setColor(visage::Color(0x30ffffff));
      for (float y = 34; y < H - 12; y += 8)
         c.fill(dx, y * k, 1 * k, 4 * k);
      for (int pass = 0; pass < 2; ++pass) {
         const double a = pass == 0 ? depth_ / 0.3 : pitch_ / 50.0;
         if (a <= 0.001)
            continue;
         visage::Path line;
         constexpr int n = 240;
         for (int i = 0; i <= n; ++i) {
            const double tt = total * i / n;
            const double grow = std::clamp((tt - delay_ / 1000.0) / 0.6, 0.0, 1.0);
            const double v = a * grow * std::sin(2 * kPi * rate_ * tt);
            const float x = (x0 + (x1 - x0) * static_cast<float>(i) / n) * k, y = (yc - amp * 0.9f * static_cast<float>(v)) * k;
            if (i == 0)
               line.moveTo(x, y);
            else
               line.lineTo(x, y);
         }
         c.setColor(pass == 0 ? glow(color_, 0.95f, 1.2f) : glow(0xffb98bff, 0.8f));
         c.fill(line.stroke((pass == 0 ? 1.8f : 1.2f) * k));
      }
      label(c, "breath", x1 - 100, H - 26, 46, color_, 10.0f, visage::Font::kRight);
      label(c, "pitch", x1 - 50, H - 26, 46, 0xffb98bff, 10.0f, visage::Font::kRight);
   }

private:
   double rate_ = 5.1, depth_ = 0.05, pitch_ = 0, delay_ = 350;
   unsigned color_ = 0xfff28c28;
};

// ------------------------------------------------------------------ voice

// How pressure becomes breath (the Press Curve), with each voice's pressure on it.
class CurveGraph : public Drawn {
public:
   explicit CurveGraph(float w) : Drawn(w) {}
   void set(double curve, std::vector<float> pressures) {
      curve_ = curve;
      pressures_ = std::move(pressures);
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const float x0 = 12, x1 = W - 12, y0 = 12, y1 = H - 26;
      c.setColor(visage::Color(0x10ffffff));
      c.segment(x0 * k, y1 * k, x1 * k, y0 * k, 1 * k, false);
      visage::Path line;
      constexpr int n = 80;
      for (int i = 0; i <= n; ++i) {
         const double x = static_cast<double>(i) / n, y = std::pow(x, curve_);
         const float px = (x0 + (x1 - x0) * static_cast<float>(x)) * k, py = (y1 - (y1 - y0) * static_cast<float>(y)) * k;
         if (i == 0)
            line.moveTo(px, py);
         else
            line.lineTo(px, py);
      }
      c.setColor(glow(kPress, 0.95f, 1.2f));
      c.fill(line.stroke(1.8f * k));
      for (float p : pressures_) {
         const float px = (x0 + (x1 - x0) * p) * k, py = (y1 - (y1 - y0) * static_cast<float>(std::pow(p, curve_))) * k;
         c.setColor(ui::steadyGlow(visage::Color(kPress).withAlpha(0.6f), {px, py}, 12 * k));
         c.circle(px - 12 * k, py - 12 * k, 24 * k);
         c.setColor(glow(0xffffffff, 0.95f));
         c.circle(px - 3.5f * k, py - 3.5f * k, 7 * k);
      }
      label(c, "PRESS", 12, H - 20, 60, kDim, 10.0f);
      label(c, "→ BREATH", W - 92, H - 20, 80, kDim, 10.0f, visage::Font::kRight);
   }

private:
   double curve_ = 1.0;
   std::vector<float> pressures_;
};

// The eight voices as lights, lit by what they're playing.
class VoiceLights : public Drawn {
public:
   explicit VoiceLights(float w) : Drawn(w) {}
   void set(const std::vector<EditorHost::Voice> &voices, int model, int mode) {
      voices_ = voices;
      model_ = model;
      mode_ = mode;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float k = s(), W = width() / k;
      const unsigned color = modelColor(model_);
      int count = 0;
      for (int i = 0; i < kVoiceCount; ++i) {
         const float cx = (W / 2 - 54 + (i % 4) * 36) * k, cy = (14 + (i / 4) * 34) * k;
         const bool on = i < static_cast<int>(voices_.size()) && voices_[i].note >= 0;
         if (on) {
            ++count;
            const float a = voices_[i].releasing ? 0.4f : 0.95f;
            c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.45f * a), {cx, cy}, 16 * k));
            c.circle(cx - 16 * k, cy - 16 * k, 32 * k);
            c.setColor(glow(color, a, 1.2f));
         } else {
            c.setColor(visage::Color(0xff2a3039));
         }
         c.circle(cx - 8 * k, cy - 8 * k, 16 * k);
      }
      char t[64];
      std::snprintf(t, sizeof(t), "%d OF %d VOICES", count, mode_ == kPoly ? kVoiceCount : 1);
      label(c, t, 0, 66, W, kDim, 10.5f, visage::Font::kCenter);
      label(c, mode_ == kPoly ? "MPE  ·  LOWER ZONE  ·  15 CH" : mode_ == kMono ? "MONO  ·  TONGUED" : "MONO  ·  SLURRED", 0, 82, W,
            kFaint, 10.0f, visage::Font::kCenter);
   }

private:
   std::vector<EditorHost::Voice> voices_;
   int model_ = kClarinet, mode_ = kPoly;
};

// The instrument's family, in its colour, and what the engine is doing.
class FamilyBadge : public Drawn {
public:
   explicit FamilyBadge(float w) : Drawn(w) {}
   void set(int model, double rate) {
      if (model == model_ && rate == rate_)
         return;
      model_ = model;
      rate_ = rate;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float W = width() / s();
      label(c, "FAMILY", 0, 0, W, kDim, 10.5f);
      label(c, kFamilyNames[model_], 0, 16, W, modelColor(model_), 17.0f);
      char t[64];
      const int os = oversamplingFor(rate_);
      std::snprintf(t, sizeof(t), "VOICES AT %.1f kHz", rate_ * os / 1000.0);
      label(c, t, 0, 46, W, kDim, 10.5f);
      label(c, os > 1 ? (std::to_string(os) + "x OVERSAMPLED").c_str() : "NATIVE RATE", 0, 62, W, kFaint, 10.0f);
   }

private:
   int model_ = kClarinet;
   double rate_ = 0.0;
};

// ------------------------------------------------------------------ the breath loops

// Each sounding voice as a loop in three dimensions: its sound now, a quarter period ago and half a
// period ago (a delay embedding), slowly turning. A steady tone draws a clean closed loop; a
// growl, a split note or breath noise frays it. Vapour gathers behind the voices.
class BreathLoops : public Drawn {
public:
   explicit BreathLoops(float w) : Drawn(w) {}

   struct Loop {
      int slot, note;
      float hz, breath, level;
      bool releasing;
      std::vector<float> samples;
   };

   void set(std::vector<Loop> loops, int model, double rate) {
      loops_ = std::move(loops);
      model_ = model;
      rate_ = rate;
   }
   // One animation step: the loops turn, and each drifts towards where its note puts it.
   void step() {
      angle_ += 0.012f;
      for (const Loop &l : loops_) {
         Place &p = places_[l.slot];
         // Low notes to the left, high to the right; alternate voices a row apart, and the harder
         // a voice blows the higher it floats.
         const float tx = std::clamp((l.note - 36) / 52.0f, 0.0f, 1.0f);
         const float ty = (l.slot % 2 ? 0.66f : 0.42f) - 0.12f * std::min(1.0f, l.breath);
         if (!p.seen) {
            p = {tx, ty, true};
         } else {
            p.x += (tx - p.x) * 0.15f;
            p.y += (ty - p.y) * 0.15f;
         }
      }
      redraw();
   }

   void draw(visage::Canvas &c) override {
      const float k = s(), W = width() / k, H = height() / k;
      const unsigned color = modelColor(model_);
      c.setColor(visage::Brush::vertical(visage::Color(0xff161c2c), visage::Color(0xff0a0d14)));
      c.roundedRectangle(0, 0, width(), height(), 10 * k);
      // Faint vapour that is always there, and brighter vapour behind each voice.
      c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.06f), {width() * 0.62f, height() * 0.58f}, 320 * k));
      c.roundedRectangle(0, 0, width(), height(), 10 * k);
      c.setColor(ui::steadyGlow(visage::Color(0xff6f8cff).withAlpha(0.05f), {width() * 0.25f, height() * 0.3f}, 280 * k));
      c.roundedRectangle(0, 0, width(), height(), 10 * k);
      for (const Loop &l : loops_) {
         const Place &p = places_[l.slot];
         const float cx = (60 + p.x * (W - 120)) * k, cy = p.y * H * k;
         const float r = (120 + 60 * l.breath) * k;
         c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.09f * std::min(1.0f, 0.3f + l.breath)), {cx, cy}, r));
         c.circle(cx - r, cy - r, 2 * r);
      }
      c.setColor(visage::Color(0x22ffffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * k, 1 * k);

      char head[64];
      std::snprintf(head, sizeof(head), "%d LOOP%s  ·  %s", static_cast<int>(loops_.size()), loops_.size() == 1 ? "" : "S",
                    kFamilyNames[model_]);
      label(c, head, W - 320, 14, 304, kDim, 11.0f, visage::Font::kRight);
      label(c, "BREATH LOOPS", 18, 14, 200, kText, 12.0f);
      if (loops_.empty()) {
         label(c, "Play a note: each voice draws its breath here", 0, H / 2 - 10, W, kFaint, 13.0f, visage::Font::kCenter);
         return;
      }
      for (const Loop &l : loops_)
         drawLoop(c, l, color, k, W, H);
   }

private:
   void drawLoop(visage::Canvas &c, const Loop &l, unsigned color, float k, float W, float H) {
      const Place &p = places_[l.slot];
      const float cx = (60 + p.x * (W - 120)) * k, cy = p.y * H * k;
      const int n = static_cast<int>(l.samples.size());
      const double period = rate_ / std::max(20.0f, l.hz);
      const int tau = std::max(1, static_cast<int>(std::lround(period / 4)));
      // Two and a half periods, newest last.
      const int span = std::min(n - 2 * tau - 1 - static_cast<int>(period / 5), static_cast<int>(period * 2.5));
      if (span < 8)
         return;
      const int first = n - span;
      // Smoothed over a fifth of a period, so the loop shows the tone's shape, not its fizz.
      const int width = std::max(1, static_cast<int>(period / 5));
      std::vector<float> x(static_cast<size_t>(n), 0.0f);
      double run = 0.0;
      for (int i = 0; i < n; ++i) {
         run += l.samples[i] - (i >= width ? l.samples[i - width] : 0.0f);
         x[i] = static_cast<float>(run / width);
      }
      float peak = 1e-6f;
      for (int i = first - 2 * tau; i < n; ++i)
         peak = std::max(peak, std::fabs(x[i]));
      const float radius = (26 + 44 * std::min(1.0f, l.breath)) * k;
      const float ca = std::cos(angle_ + l.slot), sa = std::sin(angle_ + l.slot), tilt = 0.3f;
      const float ct = std::cos(tilt), st = std::sin(tilt);
      const int stride = std::max(1, span / 220);
      std::vector<visage::Point> pts;
      for (int i = first; i < n; i += stride) {
         const float a = x[i] / peak, b = x[i - tau] / peak, z = x[i - 2 * tau] / peak;
         const float X = a * ca + z * sa, Z = -a * sa + z * ca;
         const float Y = b * ct - Z * st;
         pts.push_back({cx + X * radius, cy - Y * radius});
      }
      const float fade = l.releasing ? 0.45f : 1.0f;
      for (size_t i = 1; i < pts.size(); ++i) {
         // Older parts of the trail fainter.
         const float age = static_cast<float>(i) / pts.size();
         c.setColor(glow(color, 0.07f * fade * age));
         c.segment(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, 10 * k, true);
         c.setColor(glow(color, 0.16f * fade * age));
         c.segment(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, 4.5f * k, true);
      }
      for (size_t i = 1; i < pts.size(); ++i) {
         const float age = static_cast<float>(i) / pts.size();
         c.setColor(glow(0xfffff4e0, (0.25f + 0.7f * age) * fade, 1.0f + 0.6f * age));
         c.segment(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, 1.7f * k, true);
      }
      char t[48];
      std::snprintf(t, sizeof(t), "%s   %.0f%%", ui::noteName(l.note).c_str(), 100.0f * std::min(1.0f, l.breath / 1.3f));
      label(c, t, cx / k - 60, cy / k + radius / k + 10, 120, kFaint, 10.5f, visage::Font::kCenter);
   }

   struct Place {
      float x = 0.5f, y = 0.5f;
      bool seen = false;
   };
   std::vector<Loop> loops_;
   Place places_[kVoiceCount];
   int model_ = kClarinet;
   double rate_ = 48000.0;
   float angle_ = 0.0f;
};

// ------------------------------------------------------------------ the panel

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::UpdateNotice>({1460, 13, 164, 26});
      auto &presets = add<ui::PresetBar>({416, 11, 340, 30}, factoryPresets(), kPresetFormat, host_);
      presets.setAccent(0xfff28c28);
      presets.defaults = defaultValues();
      presets.currentName = [this] { return host_.presetName(); };
      presets.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      buildExciter();
      buildBore();
      buildPlayer();
      buildVoice();
      loops_ = &add<BreathLoops>({912, 64, 720, 536}, 720.0f);
      tips_.add(*loops_, "Each voice as a loop: its sound now, a quarter period ago and half a period ago. "
                         "A steady tone draws a clean loop; growl and breath fray it.");
      timerCallback();
      startTimer(33);
   }

   void timerCallback() override {
      const int model = modelOf(host_);
      const unsigned color = modelColor(model);
      if (model != shownModel_) {
         shownModel_ = model;
         for (ui::Knob *k : familyKnobs_)
            k->setAccent(color);
         list_->redraw();
         shape_->setModel(model);
         rangeText_->setText(rangeText(model));
         redraw();
      }
      host_.voices(voices_);
      const double rate = host_.sampleRate();

      std::vector<BreathLoops::Loop> loops;
      std::vector<float> breaths, pressures;
      int loudest = -1;
      for (int i = 0; i < kVoiceCount && i < static_cast<int>(voices_.size()); ++i) {
         const EditorHost::Voice &v = voices_[i];
         if (v.note < 0)
            continue;
         BreathLoops::Loop l{i, v.note, v.hz, v.breath, v.level, v.releasing, std::vector<float>(kLoopSamples)};
         host_.readVoice(i, l.samples.data(), l.samples.size());
         if (!v.releasing) {
            breaths.push_back(v.breath);
            pressures.push_back(std::clamp(v.pressure, 0.0f, 1.0f));
         }
         if (loudest < 0 || v.level > voices_[loudest].level)
            loudest = i;
         loops.push_back(std::move(l));
      }
      // The loudest voice's partials, by a Goertzel filter at each harmonic over its newest samples.
      std::vector<float> partials;
      double hz = 440.0 * std::pow(2.0, (62 - 69) / 12.0);
      if (loudest >= 0) {
         hz = voices_[loudest].hz;
         for (const auto &l : loops)
            if (l.slot == loudest)
               partials = harmonics(l.samples, hz, rate);
      }
      valve_->set(model, host_.paramModulated(kEmbouchure), breaths);
      bore_->set(model, hz, loudest >= 0, partials);
      info_->set(model, hz, loudest >= 0);
      curve_->set(host_.paramModulated(kCurve), pressures);
      lights_->set(voices_, model, static_cast<int>(host_.paramValue(kVoiceMode) + 0.5));
      tongue_->set(host_.paramModulated(kAttack), host_.paramModulated(kKick), host_.paramModulated(kRelease), color);
      vibrato_->set(host_.paramModulated(kVibRate), host_.paramModulated(kVibDepth), host_.paramModulated(kVibPitch),
                    host_.paramModulated(kVibDelay), color);
      badge_->set(model, rate);
      meter_->setLevel(host_.outputPeak());
      meter_->redraw();
      loops_->set(std::move(loops), model, rate);
      loops_->step();
   }

   void draw(visage::Canvas &c) override {
      const float k = scale();
      const unsigned color = modelColor(shownModel_);
      c.setColor(0xff13161b);
      c.fill(0, 0, width(), height());
      // Header.
      c.setColor(0xff0f1115);
      c.fill(0, 0, width(), kHeaderHeight * k);
      c.setColor(0xff22272f);
      c.fill(0, (kHeaderHeight - 1) * k, width(), 1 * k);
      drawLogo(c, k, color);
      c.setColor(color);
      c.text("J", ui::font(28 * k), visage::Font::kLeft, 62 * k, 6 * k, 30 * k, 40 * k);
      c.setColor(kText);
      c.text("Breath", ui::font(28 * k), visage::Font::kLeft, 77 * k, 6 * k, 120 * k, 40 * k);
      c.setColor(kDim);
      c.text("breathing physical models", ui::font(13 * k), visage::Font::kLeft, 176 * k, 16 * k, 200 * k, 22 * k);

      card(c, k, {16, 64, 880, 332}, "EXCITER", color);
      card(c, k, {16, 406, 880, 316}, "BORE", color);
      card(c, k, {16, 732, 880, 196}, "PLAYER", color);
      card(c, k, {904, 612, 728, 316}, "VOICE", color);
   }

private:
   static constexpr int kLoopSamples = 2048;

   static void card(visage::Canvas &c, float k, ui::Rect r, const char *title, unsigned color) {
      c.setColor(kCard);
      c.roundedRectangle(r.x * k, r.y * k, r.w * k, r.h * k, 10 * k);
      c.setColor(kCardBorder);
      c.roundedRectangleBorder(r.x * k, r.y * k, r.w * k, r.h * k, 10 * k, 1 * k);
      c.setColor(color);
      c.text(title, ui::font(15 * k), visage::Font::kLeft, (r.x + 16) * k, (r.y + 9) * k, 200 * k, 22 * k);
   }

   // A breath loop: a closed, slightly lopsided curve.
   static void drawLogo(visage::Canvas &c, float k, unsigned color) {
      visage::Path p;
      constexpr int n = 64;
      for (int i = 0; i <= n; ++i) {
         const float t = 2 * kPiF * i / n;
         const float x = 34 + 15 * std::sin(t) + 4 * std::sin(2 * t + 0.6f), y = 26 + 10 * std::sin(t + 1.7f) + 5 * std::sin(3 * t);
         if (i == 0)
            p.moveTo(x * k, y * k);
         else
            p.lineTo(x * k, y * k);
      }
      c.setColor(glow(color, 0.35f));
      c.fill(p.stroke(5 * k));
      c.setColor(glow(color, 1.0f, 1.3f));
      c.fill(p.stroke(2 * k));
   }

   static std::string rangeText(int model) {
      const TuningTable &t = TuningTable::get();
      char s[128];
      std::snprintf(s, sizeof(s), "SPEAKS %s – %s  ·  PAST THAT, AN OCTAVE IN",
                    ui::noteName(std::max(t.lowest(model), 21)).c_str(), ui::noteName(std::min(t.highest(model), 108)).c_str());
      return s;
   }

   static std::vector<float> harmonics(const std::vector<float> &x, double hz, double rate) {
      std::vector<float> out(BoreGraph::kPartials, 0.0f);
      const size_t n = std::min<size_t>(x.size(), 1024), first = x.size() - n;
      for (int h = 1; h <= BoreGraph::kPartials; ++h) {
         const double w = 2 * kPi * hz * h / rate;
         if (w >= kPi)
            break;
         const double coeff = 2 * std::cos(w);
         double s1 = 0, s2 = 0;
         for (size_t i = 0; i < n; ++i) {
            const double win = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
            const double s0 = x[first + i] * win + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
         }
         out[h - 1] = static_cast<float>(std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)));
      }
      return out;
   }

   ui::Knob &knob(clap_id id, const char *label, unsigned accent, ui::Rect cell, const char *tip = nullptr) {
      auto &k = add<ui::Knob>(cell, rangeOf(id));
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      if (tip)
         tips_.add(k, tip);
      return k;
   }
   ui::Knob &familyKnob(clap_id id, const char *label, ui::Rect cell, const char *tip = nullptr) {
      ui::Knob &k = knob(id, label, modelColor(modelOf(host_)), cell, tip);
      k.setTextSizes(9.5f, 11.0f, 9.0f);
      familyKnobs_.push_back(&k);
      return k;
   }

   void buildExciter() {
      list_ = &add<InstrumentList>({32, 100, 190, 280}, 190.0f, host_);
      tips_.add(*list_, "The instrument: how the air is set vibrating, and the tube it plays into.");
      valve_ = &add<ValveGraph>({236, 100, 306, 280}, 306.0f);
      tips_.add(*valve_, "The exciter's valve: how the air through it answers the pressure across it. "
                         "Each dot is a sounding voice.");
      const auto top = ui::row({556, 92, 326, 120}, 4), bottom = ui::row({556, 220, 326, 120}, 4);
      familyKnob(kBreath, "BREATH", top[0], "How hard the player blows at full pressure (or velocity).");
      familyKnob(kEmbouchure, "EMBOUCHURE", top[1],
                 "Lip and jaw: the jet's angle on the flute, the bite on a reed, the lips' tension on brass. "
                 "MPE slide moves it.");
      familyKnob(kNoise, "NOISE", top[2], "Breath noise and turbulence.");
      familyKnob(kEdge, "EDGE", top[3],
                 "The forte bite: the flute's 2nd harmonic, the reeds' growl, the brass wave steepening into its blare.");
      familyKnob(kKick, "KICK", bottom[0], "The burst of pressure as the tongue lets the air go.");
      familyKnob(kAttack, "ATTACK", bottom[1], "How fast the breath rises.");
      familyKnob(kRelease, "RELEASE", bottom[2], "How fast the breath dies away after the note.");
      familyKnob(kGlide, "GLIDE", bottom[3], "How long a slur takes to reach the next note (a finger lands in a few ms).");
      rangeText_ = &add<ui::Label>({556, 352, 326, 16}, rangeText(modelOf(host_)), kFaint, visage::Font::kLeft, 0.68f);
   }

   void buildBore() {
      shape_ = &add<BoreShape>({112, 414, 360, 26}, 360.0f);
      shape_->setModel(modelOf(host_));
      tips_.add(*shape_, "The bore's shape, which sets which harmonics it resonates on.");
      bore_ = &add<BoreGraph>({32, 452, 848, 158}, 848.0f);
      tips_.add(*bore_, "The bore's resonances (its input impedance), with the loudest note's partials as it sounds.");
      const auto cells = ui::row({32, 618, 420, 100}, 3);
      knob(kTune, "TUNE", kDim, cells[0], "Transposes in semitones.");
      knob(kFine, "FINE", kDim, cells[1], "Fine tuning in cents.");
      knob(kBright, "BRIGHT", kDim, cells[2], "A tilt above 1.5 kHz on the output.");
      info_ = &add<BoreInfo>({470, 624, 410, 88}, 410.0f);
   }

   void buildPlayer() {
      tongue_ = &add<TongueGraph>({32, 766, 270, 152}, 270.0f);
      vibrato_ = &add<VibratoGraph>({314, 766, 270, 152}, 270.0f);
      const auto cells = ui::row({594, 786, 290, 110}, 4);
      familyKnob(kVibRate, "RATE", cells[0], "Vibrato rate. A player's wanders a little.");
      familyKnob(kVibDepth, "DEPTH", cells[1], "Vibrato in the breath.");
      familyKnob(kVibPitch, "PITCH", cells[2], "Vibrato in the pitch, in cents (the lips' or jaw's).");
      familyKnob(kVibDelay, "DELAY", cells[3], "How long a note is held before the vibrato grows in.");
   }

   void buildVoice() {
      add<ui::Label>({1240, 622, 120, 16}, "VOICES", kDim, visage::Font::kRight, 0.68f);
      auto &mode = add<ui::SegmentedSwitch>({1368, 618, 252, 26}, std::vector<std::string>{"POLY", "MONO", "LEGATO"});
      mode.setAccent(0xfff28c28);
      bindings_.bind(mode, kVoiceMode);
      tips_.add(mode, "Poly: eight voices. Mono: one, tongued between notes. Legato: one, slurred between notes.");
      add<ui::Label>({1240, 656, 120, 16}, "BREATH FROM", kDim, visage::Font::kRight, 0.68f);
      auto &source = add<ui::SegmentedSwitch>({1368, 652, 252, 26}, std::vector<std::string>{"AUTO", "PRESS", "VEL", "INPUT"});
      source.setAccent(kPress);
      bindings_.bind(source, kBreathSource);
      tips_.add(source, "Where the breath comes from. Auto: velocity, until pressure, a breath controller (CC 2) or Breath In takes over. Input: only Breath In.");

      curve_ = &add<CurveGraph>({920, 652, 200, 124}, 200.0f);
      tips_.add(*curve_, "How pressure becomes breath. Each dot is a voice's pressure.");
      lights_ = &add<VoiceLights>({1128, 690, 200, 100}, 200.0f);
      badge_ = &add<FamilyBadge>({1400, 692, 180, 90}, 180.0f);
      meter_ = &add<ui::Meter>({1600, 690, 20, 100});
      meter_->setRange(-48.0f, 6.0f);
      tips_.add(*meter_, "The output level.");

      const auto cells = ui::row({920, 800, 700, 120}, 5);
      knob(kBreathIn, "BREATH IN", kPress, cells[0],
           "A breath to blow from: modulate it from a CV, an LFO or automation. Heard with Breath From on Input (or Auto).");
      knob(kCurve, "CURVE", kPress, cells[1], "The press curve: above 1 you have to press harder.");
      knob(kSlide, "SLIDE", 0xff8fe8a8, cells[2], "How far MPE slide (CC 74) moves the embouchure.");
      knob(kBendRange, "BEND", 0xffb98bff, cells[3], "MPE pitch bend range of the note channels, in semitones.");
      knob(kLevel, "LEVEL", 0xfff28c28, cells[4], "Output level.");
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   InstrumentList *list_ = nullptr;
   ValveGraph *valve_ = nullptr;
   BoreGraph *bore_ = nullptr;
   BoreShape *shape_ = nullptr;
   BoreInfo *info_ = nullptr;
   TongueGraph *tongue_ = nullptr;
   VibratoGraph *vibrato_ = nullptr;
   CurveGraph *curve_ = nullptr;
   VoiceLights *lights_ = nullptr;
   BreathLoops *loops_ = nullptr;
   ui::Meter *meter_ = nullptr;
   FamilyBadge *badge_ = nullptr;
   ui::Label *rangeText_ = nullptr;
   std::vector<ui::Knob *> familyKnobs_;
   std::vector<EditorHost::Voice> voices_;
   int shownModel_ = -1;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, int frames, const char *pngPath) {
   Panel panel(host);
   for (int i = 0; i < frames; ++i)
      panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jbreath
