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
#include <jclap/ui/number_box.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/button.h>

#include <chrono>

#include <visage/app.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/frame_pacer.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "params.h"
#include "presets.h"
#include "randomize.h"
#include "vapor.h"
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
// The HYBRID tab's subtractive voice.
constexpr unsigned kSynthColor = 0xffb98bff;

// Each instrument's family colour: the jet cyan, the single reeds orange, the double reed violet,
// the lips coral, the alien's bowed string an acid green.
constexpr unsigned kModelColors[kModelCount] = {0xff6fe0ff, 0xfff28c28, 0xfff2a54a, 0xffa98bff, 0xffff7a59, 0xff9dff5c};
unsigned modelColor(int model) { return kModelColors[std::clamp(model, 0, kModelCount - 1)]; }

const char *const kFamilyNames[kModelCount] = {"JET", "REED", "REED", "DOUBLE REED", "LIPS", "BOW"};
const char *const kModelSubtitles[kModelCount] = {"jet  ·  open pipe", "single reed  ·  cylinder", "single reed  ·  cone",
                                                  "double reed  ·  narrow cone", "lips  ·  flared horn",
                                                  "bowed string  ·  shifting throat"};

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

   // The width in design units, when the frame is moved to a new size.
   void setDesignWidth(float w) { designWidth_ = w; }

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
   InstrumentList(float w, EditorHost &host, clap_id param = kModel) : Drawn(w), host_(host), param_(param) {}

   void draw(visage::Canvas &c) override {
      const float k = s(), rowH = rowHeight();
      const int selected = std::clamp(static_cast<int>(host_.paramValue(param_) + 0.5), 0, rows() - 1);
      const bool waves = param_ == kHybridModel;
      static const char *const kWaveSubtitles[] = {"Pure and round", "Soft, odd harmonics", "Bright, every harmonic",
                                                   "Hollow, odd harmonics", "Nasal, a quarter wide"};
      for (int i = 0; i < rows(); ++i) {
         const float y = i * (rowH + kGap);
         const unsigned color = waves ? kSynthColor : modelColor(i);
         const bool on = i == selected;
         c.setColor(on ? glow(color, 0.16f) : visage::Color(i == hover_ ? 0xff262c35 : 0xff1f242c));
         c.roundedRectangle(0, y * k, width(), rowH * k, 6 * k);
         c.setColor(on ? glow(color, 0.85f, 1.2f) : visage::Color(0x18ffffff));
         c.roundedRectangleBorder(0, y * k, width(), rowH * k, 6 * k, (on ? 1.5f : 1.0f) * k);
         label(c, waves ? kHybridWaveNames[i] : kModelNames[i], 12, y + 5, 140, on ? color : kText, 15.0f);
         label(c, waves ? kWaveSubtitles[i] : kModelSubtitles[i], 12, y + 25, 150, kDim, 10.5f);
         if (waves)
            drawWave(c, i, (designW() - 30) * k, (y + rowH / 2) * k, k, on ? color : kDim);
         else
            drawIcon(c, i, (designW() - 30) * k, (y + rowH / 2) * k, k, on ? color : kDim);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const int i = rowAt(e.position.y);
      if (i < 0)
         return;
      host_.beginEdit(param_);
      host_.setParam(param_, i);
      host_.endEdit(param_);
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
   // The instruments, or the HYBRID tab's five waves.
   int rows() const { return param_ == kHybridModel ? 5 : kModelCount; }
   float designW() const { return width() / s(); }
   float rowHeight() const { return (height() / s() - kGap * (rows() - 1)) / rows(); }
   int rowAt(float y) const {
      const int i = static_cast<int>(y / s() / (rowHeight() + kGap));
      return i >= 0 && i < rows() ? i : -1;
   }

   // Two cycles of the wave, 36 x 16 design units, centred on (x, y).
   static void drawWave(visage::Canvas &c, int wave, float x, float y, float k, unsigned color) {
      visage::Path path;
      constexpr int n = 64;
      for (int i = 0; i <= n; ++i) {
         const double t = 2.0 * i / n, ph = t - std::floor(t);
         double v = 0;
         switch (wave) {
         case 0: v = std::sin(2 * kPi * ph); break;
         case 1: v = 1 - 4 * std::fabs(ph - 0.5); break;
         case 2: v = 2 * ph - 1; break;
         case 3: v = ph < 0.5 ? 1 : -1; break;
         default: v = ph < 0.25 ? 1 : -1; break;
         }
         const float px = x + (-18 + 36.0f * i / n) * k, py = y - static_cast<float>(v) * 7 * k;
         if (i == 0)
            path.moveTo(px, py);
         else
            path.lineTo(px, py);
      }
      c.setColor(color);
      c.fill(path.stroke(1.6f * k));
   }

   // The instrument, side on, in the row's colour (the SVGs draw in currentColor).
   static void drawIcon(visage::Canvas &c, int model, float x, float y, float k, unsigned color) {
      static const char *const kSources[kModelCount] = {
   // flute
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="10.5" width="42" height="3" rx="1.5"/><ellipse cx="9" cy="10.2" rx="2.4" ry="1.1"/><path d="M38.5 10.5 L38.5 13.5"/><circle cx="18" cy="12" r="0.75" fill="currentColor" stroke="none"/><circle cx="22" cy="12" r="0.75" fill="currentColor" stroke="none"/><circle cx="26" cy="12" r="0.75" fill="currentColor" stroke="none"/><circle cx="30" cy="12" r="0.75" fill="currentColor" stroke="none"/><circle cx="34" cy="12" r="0.75" fill="currentColor" stroke="none"/></svg>)svg",
   // clarinet
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 11.4 L7.5 10.6 L7.5 13.4 L2.5 12.6 Z"/><rect x="7.5" y="10" width="4.5" height="4" rx="0.8"/><path d="M12 10.6 L37 10.6 Q41 10.4 45 7.5 L45 16.5 Q41 13.6 37 13.4 L12 13.4"/><circle cx="17" cy="12" r="0.7" fill="currentColor" stroke="none"/><circle cx="21" cy="12" r="0.7" fill="currentColor" stroke="none"/><circle cx="25" cy="12" r="0.7" fill="currentColor" stroke="none"/><circle cx="29" cy="12" r="0.7" fill="currentColor" stroke="none"/><circle cx="33" cy="12" r="0.7" fill="currentColor" stroke="none"/></svg>)svg",
   // sax
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 3.4 L4.8 3.1 M3.6 3.3 Q8 3 11 5.7"/><path d="M11 4.6 L27.5 15.3 Q30.5 17.3 33.2 16.3 Q35 15.5 34.8 12 Q34.6 9 31.5 6 L44.5 6 Q39 9 38.8 13 Q38.8 18.6 35.2 20.2 Q30.5 22.2 25.5 17.8 L11 6.8 Z"/><circle cx="15.5" cy="8.3" r="0.75" fill="currentColor" stroke="none"/><circle cx="19.5" cy="11" r="0.75" fill="currentColor" stroke="none"/><circle cx="23.5" cy="13.7" r="0.75" fill="currentColor" stroke="none"/></svg>)svg",
   // oboe
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><g transform="translate(24 12) rotate(24) scale(0.92) translate(-24 -12)"><path d="M2.2 10 Q1.3 12 2.2 14 L6.5 12.6 L6.5 11.4 Z" fill="currentColor" fill-opacity="0.25"/><path d="M2.6 12 L6.5 12" stroke-width="0.9"/><rect x="6.5" y="10.8" width="2.8" height="2.4" rx="0.5" fill="currentColor" stroke="none"/><path d="M9.3 11.1 L37 10.2 Q41 9.9 42.5 9.1 L44.8 8.8 L44.8 15.2 L42.5 14.9 Q41 14.1 37 13.8 L9.3 12.9"/><path d="M20.5 10.2 L20.5 13.8 M31 9.9 L31 14.1" stroke-width="1.3"/><circle cx="13" cy="12" r="0.65" fill="currentColor" stroke="none"/><circle cx="16.5" cy="12" r="0.65" fill="currentColor" stroke="none"/><circle cx="24" cy="12" r="0.65" fill="currentColor" stroke="none"/><circle cx="27.5" cy="12" r="0.65" fill="currentColor" stroke="none"/><circle cx="34" cy="12" r="0.65" fill="currentColor" stroke="none"/></g></svg>)svg",
   // trumpet
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 8.2 L4.5 9 L36 9 M2.5 9.8 L4.5 9"/><path d="M36 8.2 Q41 8 45 3.5 L45 14.5 Q41 10 36 9.8"/><path d="M12 9 Q8.5 9 8.5 13 Q8.5 17 12.5 17 L29 17 Q32.5 17 32.5 13 L32.5 9"/><path d="M17 6 L17 17 M21 6 L21 17 M25 6 L25 17" stroke-width="2.2"/><circle cx="17" cy="4.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="21" cy="4.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="25" cy="4.6" r="1.1" fill="currentColor" stroke="none"/></svg>)svg",
   // alien: a string between its nut and bridge, a bow crossing it near the bridge, and the throat
   // it sounds through, an open mouth with its resonances rippling inside
   R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="24" viewBox="0 0 48 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9.5 L3 14.5 M29 9.5 L29 14.5"/><path d="M3 12 L29 12" stroke-width="1.1"/><path d="M14 3.5 L24 20.5" stroke-width="1.3"/><path d="M15.6 3 L25.6 20" stroke-width="0.8"/><path d="M29 12 L32 12"/><path d="M32 12 Q34 4.5 40 4.5 Q46 4.5 46 12 Q46 19.5 40 19.5 Q34 19.5 32 12 Z"/><path d="M35.5 12 Q37 9 38.5 12 Q40 15 41.5 12 Q43 9 44 11" stroke-width="1.1"/></svg>)svg",
      };
      static const std::vector<visage::Svg> kIcons = [] {
         std::vector<visage::Svg> icons;
         for (const char *source : kSources)
            icons.emplace_back(reinterpret_cast<const unsigned char *>(source), static_cast<int>(std::strlen(source)));
         return icons;
      }();
      // 40 x 20 design units, centred on (x, y).
      const float w = 40 * k, h = 20 * k;
      c.setColor(color);
      c.svg(kIcons[static_cast<size_t>(std::clamp(model, 0, kModelCount - 1))], x - w / 2, y - h / 2, w, h);
   }

   EditorHost &host_;
   clap_id param_;
   int hover_ = -1;
};

// The Alien's throat: its ring of eight shapes, where each note started (the Voice knob, a hollow
// ring) and where each sounding voice's throat has travelled to since (a dot).
class ThroatRing : public Drawn {
public:
   explicit ThroatRing(float w) : Drawn(w) {}
   void set(double voice, std::vector<float> positions, bool lit) {
      voice_ = voice;
      positions_ = std::move(positions);
      lit_ = lit;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const unsigned color = lit_ ? modelColor(kAlien) : kFaint;
      label(c, "THROAT", 12, 8, W - 24, kDim, 10.5f);
      const float cx = W / 2, cy = 34 + (H - 34) / 2 - 4, r = std::min(W, H - 40) / 2 - 30;
      const auto at = [&](double pos, float radius) {
         const double a = 2 * kPi * pos / kAlienShapes - kPi / 2;
         return std::pair<float, float>{cx + radius * static_cast<float>(std::cos(a)), cy + radius * static_cast<float>(std::sin(a))};
      };
      // The ring, and the eight shapes on it (the vowels plain, the others the instrument's colour).
      visage::Path ring;
      for (int i = 0; i <= 96; ++i) {
         const auto [x, y] = at(kAlienShapes * i / 96.0, r);
         if (i == 0)
            ring.moveTo(x * k, y * k);
         else
            ring.lineTo(x * k, y * k);
      }
      c.setColor(visage::Color(0x30ffffff));
      c.fill(ring.stroke(1.5f * k));
      static const bool vowel[kAlienShapes] = {true, false, true, false, true, true, false, true};
      for (int i = 0; i < kAlienShapes; ++i) {
         const auto [x, y] = at(i, r);
         c.setColor(vowel[i] ? visage::Color(0xff3a414d) : glow(color, 0.55f));
         c.circle((x - 3) * k, (y - 3) * k, 6 * k);
         const auto [lx, ly] = at(i, r + 17);
         label(c, kAlienShapeNames[i], lx - 30, ly - 8, 60, vowel[i] ? kDim : color, 10.0f, visage::Font::kCenter);
      }
      // Where each note starts.
      {
         const auto [x, y] = at(voice_ * kAlienShapes, r);
         visage::Path hoop;
         for (int m = 0; m <= 24; ++m) {
            const double a = 2 * kPi * m / 24;
            const float hx = (x + 7 * static_cast<float>(std::cos(a))) * k, hy = (y + 7 * static_cast<float>(std::sin(a))) * k;
            if (m == 0)
               hoop.moveTo(hx, hy);
            else
               hoop.lineTo(hx, hy);
         }
         c.setColor(glow(color, 0.9f));
         c.fill(hoop.stroke(1.6f * k));
      }
      // Each voice's throat now.
      for (float pos : positions_) {
         const auto [x, y] = at(pos, r);
         c.setColor(ui::steadyGlow(visage::Color(color).withAlpha(0.5f), {x * k, y * k}, 14 * k));
         c.circle((x - 14) * k, (y - 14) * k, 28 * k);
         c.setColor(glow(0xffffffff, 0.95f));
         c.circle((x - 4) * k, (y - 4) * k, 8 * k);
      }
   }

private:
   double voice_ = 0.0;
   std::vector<float> positions_;
   bool lit_ = true;
};

// The throat's resonances as they stand for the loudest voice (or where a note would start): its
// response from 50 Hz to 10 kHz, and the note's harmonics along the bottom, so you can see which of
// them each resonance lifts.
class FormantGraph : public Drawn {
public:
   explicit FormantGraph(float w) : Drawn(w) {}
   void set(double pos, double shift, double sharp, double hz, bool lit) {
      pos_ = pos;
      shift_ = shift;
      sharp_ = sharp;
      hz_ = hz;
      lit_ = lit;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const float x0 = 14, x1 = W - 14, y0 = 34, y1 = H - 30;
      const unsigned color = lit_ ? modelColor(kAlien) : kFaint;
      const int i = static_cast<int>(std::floor(pos_)) % kAlienShapes, j = (i + 1) % kAlienShapes;
      const int pct = static_cast<int>(std::lround(100.0 * (pos_ - std::floor(pos_))));
      std::string title = std::string("RESONANCES  ·  ") + kAlienShapeNames[i];
      if (pct > 2 && pct < 98)
         title += std::string(" to ") + kAlienShapeNames[j] + " " + std::to_string(pct) + "%";
      label(c, title, 12, 8, W - 24, kDim, 10.5f);
      c.setColor(visage::Color(0x0effffff));
      for (double f : {100.0, 1000.0, 5000.0})
         c.fill((x0 + (x1 - x0) * xOf(f)) * k, y0 * k, 1 * k, (y1 - y0) * k);
      label(c, "100 Hz", x0 + (x1 - x0) * xOf(100) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      label(c, "1 kHz", x0 + (x1 - x0) * xOf(1000) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      label(c, "5 kHz", x0 + (x1 - x0) * xOf(5000) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      AlienThroat t;
      alienThroat(pos_, shift_, sharp_, t);
      const auto db = [&](double f) {
         return 20.0 * std::log10(t.norm * t.cascade(f) + 1e-9);
      };
      const auto py = [&](double d) { return (y1 - (y1 - y0) * static_cast<float>(std::clamp((d + 64.0) / 84.0, 0.0, 1.0))) * k; };
      visage::Path line, area;
      constexpr int n = 160;
      for (int m = 0; m <= n; ++m) {
         const double x = static_cast<double>(m) / n, f = kLow * std::pow(kHigh / kLow, x);
         const float px = (x0 + (x1 - x0) * static_cast<float>(x)) * k, y = py(db(f));
         if (m == 0) {
            line.moveTo(px, y);
            area.moveTo(px, y1 * k);
            area.lineTo(px, y);
         } else {
            line.lineTo(px, y);
            area.lineTo(px, y);
         }
      }
      area.lineTo(x1 * k, y1 * k);
      area.close();
      c.setColor(visage::Brush::vertical(glow(color, 0.35f), glow(color, 0.02f)));
      c.fill(area);
      c.setColor(glow(color, 0.95f, 1.3f));
      c.fill(line.stroke(2.0f * k));
      // The note's harmonics, as tall as the throat lifts them.
      if (hz_ > 0.0)
         for (int h = 1; h * hz_ < kHigh; ++h) {
            const double f = h * hz_;
            if (f < kLow)
               continue;
            const float px = (x0 + (x1 - x0) * xOf(f)) * k, top = py(db(f));
            c.setColor(visage::Color(0x50ffffff));
            c.fill(px - 0.75f * k, top, 1.5f * k, y1 * k - top);
         }
   }

private:
   static constexpr double kLow = 50.0, kHigh = 10000.0;
   static float xOf(double f) { return static_cast<float>(std::log(f / kLow) / std::log(kHigh / kLow)); }
   double pos_ = 0.0, shift_ = 0.0, sharp_ = 0.5, hz_ = 0.0;
   bool lit_ = true;
};

// The exciter's valve: how the air through it answers the pressure across it, with a dot where
// each sounding voice is blowing.
// The HYBRID voice's lowpass: its response from 20 Hz to 20 kHz, where it sits now and how far
// the breath opens it, with each sounding voice's cutoff as a dot.
class FilterGraph : public Drawn {
public:
   explicit FilterGraph(float w) : Drawn(w) {}
   void set(double cutoff, double resonance, double breathAmount, std::vector<float> opens) {
      cutoff_ = cutoff;
      resonance_ = resonance;
      amount_ = breathAmount;
      opens_ = std::move(opens);
      redraw();
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const float x0 = 14, x1 = W - 14, y0 = 34, y1 = H - 26;
      c.setColor(visage::Color(0x0effffff));
      for (double f : {100.0, 1000.0, 10000.0})
         c.fill((x0 + (x1 - x0) * xOf(f)) * k, y0 * k, 1 * k, (y1 - y0) * k);
      label(c, "LOWPASS  ·  24 dB", 12, 8, W - 24, kDim, 10.5f);
      label(c, "100 Hz", x0 + (x1 - x0) * xOf(100) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      label(c, "1 kHz", x0 + (x1 - x0) * xOf(1000) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      label(c, "10 kHz", x0 + (x1 - x0) * xOf(10000) - 30, H - 20, 60, kFaint, 10.0f, visage::Font::kCenter);
      const auto curve = [&](double cut, visage::Path &line, visage::Path *area) {
         constexpr int n = 140;
         for (int i = 0; i <= n; ++i) {
            const double x = static_cast<double>(i) / n, f = 20.0 * std::pow(1000.0, x);
            const float px = (x0 + (x1 - x0) * static_cast<float>(x)) * k;
            const float py = (y1 - (y1 - y0) * static_cast<float>(std::clamp((dbAt(f, cut) + 48.0) / 60.0, 0.0, 1.0))) * k;
            if (i == 0) {
               line.moveTo(px, py);
               if (area) {
                  area->moveTo(px, y1 * k);
                  area->lineTo(px, py);
               }
            } else {
               line.lineTo(px, py);
               if (area)
                  area->lineTo(px, py);
            }
         }
         if (area) {
            area->lineTo(x1 * k, y1 * k);
            area->close();
         }
      };
      // How far the breath opens it: the curve at the softest and the hardest breath, faint.
      for (double blow : {0.0, 1.0}) {
         visage::Path line;
         curve(cutoff_ * std::pow(2.0, amount_ * 6.0 * (blow - 0.7)), line, nullptr);
         c.setColor(visage::Color(0x30ffffff));
         c.fill(line.stroke(1.0f * k));
      }
      visage::Path line, area;
      curve(cutoff_, line, &area);
      c.setColor(visage::Brush::vertical(glow(kSynthColor, 0.35f), glow(kSynthColor, 0.02f)));
      c.fill(area);
      c.setColor(glow(kSynthColor, 0.95f, 1.3f));
      c.fill(line.stroke(2.0f * k));
      for (float open : opens_) {
         const double f = std::clamp(cutoff_ * open, 20.0, 20000.0);
         const float cx = (x0 + (x1 - x0) * xOf(f)) * k;
         const float cy = (y1 - (y1 - y0) * static_cast<float>(std::clamp((dbAt(f, cutoff_ * open) + 48.0) / 60.0, 0.0, 1.0))) * k;
         c.setColor(ui::steadyGlow(visage::Color(kSynthColor).withAlpha(0.5f), {cx, cy}, 14 * k));
         c.circle(cx - 14 * k, cy - 14 * k, 28 * k);
         c.setColor(visage::Color(0xffffffff));
         c.circle(cx - 3.5f * k, cy - 3.5f * k, 7 * k);
      }
   }

private:
   static float xOf(double f) { return static_cast<float>(std::log(f / 20.0) / std::log(1000.0)); }
   // A 4-pole lowpass's level at f in dB, its peak set by the resonance.
   double dbAt(double f, double cut) const {
      const double r = f / std::max(cut, 1.0), q = 0.5 + 6.0 * resonance_ * resonance_;
      const double one = 1.0 / std::sqrt((1 - r * r) * (1 - r * r) + (r / q) * (r / q));
      return 2.0 * 20.0 * std::log10(one + 1e-9);
   }
   double cutoff_ = 1800, resonance_ = 0.2, amount_ = 0.6;
   std::vector<float> opens_;
};

class ValveGraph : public Drawn {
public:
   explicit ValveGraph(float w) : Drawn(w) {}

   void set(int model, double embouchure, std::vector<float> breaths, double stiffness = 0.5, double aperture = 0.5,
            double bite = 1.0) {
      model_ = model;
      embouchure_ = embouchure;
      bite_ = bite;
      stiffness_ = stiffness;
      aperture_ = aperture;
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
   const char *yName() const {
      return model_ == kFlute ? "JET SPEED" : model_ == kTrumpet ? "AIR THROUGH THE LIPS" : model_ == kAlien ? "THE BOW'S GRIP" : "AIR FLOW";
   }
   const char *xName() const {
      switch (model_) {
      case kFlute:
         return "BREATH  →";
      case kTrumpet:
         return "MOUTH PRESSURE  →";
      case kAlien:
         return "THE STRING SLIPPING PAST THE BOW  →";
      default:
         return "PRESSURE ACROSS THE REED  →";
      }
   }
   const char *shutName() const { return model_ == kFlute ? "OVERBLOWS" : model_ == kAlien ? "SLIPS" : "REED SHUTS"; }
   double xMax() const { return model_ == kFlute || model_ == kTrumpet ? 1.3 : 1.25; }
   // Where the curve turns over: the reed closes, or the jet is fast enough to jump a register.
   double shutAt() const {
      switch (model_) {
      case kFlute:
         return 1.05;
      case kAlien: {
         // Where the grip peaks: past it, the string breaks free and slips.
         double best = 0.0, at = 0.0;
         for (int i = 1; i <= 200; ++i) {
            const double x = xMax() * i / 200.0, g = grip(x);
            if (g > best) {
               best = g;
               at = x;
            }
         }
         return at;
      }
      case kClarinet:
      case kSax:
      case kOboe:
         // A softer reed curls shut later.
         return 1.0 + 0.12 * (0.5 - stiffness_);
      default:
         return -1.0;
      }
   }
   double curve(double x) const {
      switch (model_) {
      case kFlute: // jet speed grows as the square root of the pressure
         return std::sqrt(x / 1.3) * 0.92;
      case kTrumpet: // the lips open with the pressure, and the flow through them as its root: x^1.5
         return std::min(1.0, 0.92 * std::pow(x / 1.3, 1.5) * (0.5 + aperture_));
      case kAlien: { // the bow's friction (models.h), normalised to its peak at the middle Bite
         double peak = 0.0;
         for (int i = 1; i <= 200; ++i)
            peak = std::max(peak, grip(xMax() * i / 200.0, 1.0));
         return std::min(1.0, 0.92 * grip(x) / peak);
      }
      default: { // a reed valve: u = (1 - W) sqrt(W), shut at W = 1; normalised to its peak
         const double shut = shutAt(), w = std::min(x / shut, 1.0), open = 0.6 + 0.8 * aperture_;
         const double soft = model_ == kClarinet ? 1.0 : model_ == kSax ? 0.85 + 0.3 * (1 - embouchure_) : 0.7;
         return std::min(1.0, 0.95 * (1 - w) * std::sqrt(w) / 0.3849 * soft * open);
      }
      }
   }
   // A voice's breath, as a point on the x axis (the Alien's: the bow's speed, the string's slip).
   double operating(float breath) const {
      return model_ == kClarinet ? 0.7 * breath : model_ == kAlien ? 0.5 * breath : model_ == kFlute || model_ == kTrumpet ? breath : 0.75 * breath;
   }
   // The Alien's bow: its grip on the string as the string slips past it at x (the model's bow
   // table over the axis, 0..1.25 as 0..0.6 of slip), the Bite pressing it harder.
   double grip(double x, double bite = -1.0) const {
      const double force = std::clamp(0.4 + 0.35 * ((bite < 0.0 ? bite_ : bite) - 1.0), 0.04, 0.95), slope = 5.0 - 4.0 * force;
      const double dv = 0.6 * x / xMax(), f = std::fabs(dv * slope) + 0.75;
      return dv * std::clamp(1.0 / (f * f * f * f), 0.01, 0.98);
   }

   int model_ = kClarinet;
   double embouchure_ = 0.5, stiffness_ = 0.5, aperture_ = 0.5, bite_ = 1.0;
   std::vector<float> breaths_;
};

// ------------------------------------------------------------------ bore

// The bore's input impedance, from the same loop the model runs (its length, reflection and losses),
// with the partials of the loudest sounding note as they're really coming out.
class BoreGraph : public Drawn {
public:
   explicit BoreGraph(float w) : Drawn(w) {}

   void set(int model, double hz, bool playing, std::vector<float> partials, double loss = 0.04, double cutoff = 1.0,
            double cone = 0.5) {
      model_ = model;
      cone_ = cone;
      hz_ = hz;
      playing_ = playing;
      partials_ = std::move(partials);
      loss_ = loss;
      cut_ = cutoff;
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
      Character ch;
      ch.loss = static_cast<float>(loss_);
      const double k = cut_;
      switch (model_) {
      case kFlute:
         r = static_cast<double>(ch.keep(0.97f)) * onePole(f, 3.2 * hz_ * k);
         break;
      case kClarinet: {
         const double cut = std::max(1300.0, 2.2 * hz_) * k;
         r = -static_cast<double>(ch.keep(0.96f)) * onePole(f, cut) * onePole(f, cut);
         period = 0.5 / hz_;
         break;
      }
      case kSax:
         r = static_cast<double>(ch.keep(0.95f)) * onePole(f, 5200 * k);
         break;
      case kOboe:
         r = static_cast<double>(ch.keep(0.95f)) * onePole(f, 3800 * k);
         break;
      case kAlien: {
         // The string: both ends turn its waves over (so every harmonic), through its lowpass, and a
         // stiff string's two allpasses stretch its partials apart (the Cone knob).
         const double a = -0.7 * std::pow(std::clamp(cone_, 0.0, 1.0), 1.5);
         const std::complex<double> z = std::polar(1.0, -2 * kPi * f / kFs), z0 = std::polar(1.0, -2 * kPi * hz_ / kFs);
         const std::complex<double> ap = (a + z) / (1.0 + a * z), ap0 = (a + z0) / (1.0 + a * z0);
         r = static_cast<double>(ch.keep(0.995f)) * onePole(f, 14 * hz_ * k) * ap * ap;
         period = 1.0 / hz_ + 2.0 * std::arg(ap0) / (2 * kPi * hz_);
         break;
      }
      default:
         r = static_cast<double>(ch.keep(0.985f)) * (onePole(f, 8 * hz_ * k) - 2.0 * onePole(f, 20));
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
      case kAlien:
         return cone_ > 0.6 ? "STIFF STRING: PARTIALS STRETCHED APART" : "STRING: RESONANCES ON EVERY HARMONIC";
      default:
         return "FLARED HORN: HARMONIC RESONANCES, NO PEDAL";
      }
   }

   int model_ = kClarinet;
   double hz_ = 293.66, loss_ = 0.04, cut_ = 1.0, cone_ = 0.5;
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
      // (The Alien's string has none of these.)
      const int lit = model_ == kFlute ? 0 : model_ == kClarinet ? 1 : model_ == kTrumpet ? 3 : model_ == kAlien ? -1 : 2;
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
// Screenshots and the benchmark solve the smoke on the UI thread, so a picture is the same every time.
bool gSyncSmoke = false;

class BreathLoops : public Drawn {
public:
   BreathLoops(float w, float h) : Drawn(w), W_(w), H_(h) { layoutField(w, h); }

   // A line of help along the top (MAP mode's), in its colour.
   std::string hint;
   unsigned hintColor = kPress;

   // The look (the AIR view's knobs): smoke or ink, its palette, how turbulent the air is, how
   // fast the smoke rises, how much it curls, how long it hangs, and the loops' line and glow.
   struct Look {
      int ink = 0, palette = 0;
      float turbulence = 0.42f, rise = 0.55f, swirl = 0.38f, tail = 4.0f, line = 0.4f, glow = 0.5f;
   };
   Look look;
   // Filling the whole window (the AIR view): the voices sounding are listed in a corner.
   bool full = false;

   struct Loop {
      int slot, note;
      float hz, breath, level;
      bool releasing;
      std::vector<float> samples;
      // The note's touch: strike (velocity), press and slide, 0..1, and a random -1..1 of its own.
      float strike = 0.5f, press = 0.0f, slide = 0.5f, random = 0.0f;
   };
   // What places the loops along each axis and sets their size (LoopSource), and the smallest size,
   // as a share of the largest.
   struct Placing {
      int x = kLoopPitch, y = kLoopBreath, size = kLoopBreath;
      float sizeMin = 0.25f;
   };
   Placing placing;

   // A new size in design units, after the panel moved it.
   void resize(float w, float h) {
      W_ = w;
      H_ = h;
      setDesignWidth(w);
      layoutField(w, h);
   }
   void set(std::vector<Loop> loops, int model, double rate) {
      loops_ = std::move(loops);
      model_ = model;
      rate_ = rate;
   }
   // How many cells the smoke's grid has (its cost a step).
   int cells() const { return runner_->cells(); }

   // One animation step of dt seconds: the loops turn, each drifts towards where its note puts it,
   // sheds warm smoke off its line, and the smoke rises (or ink sinks), curls, cools and thins.
   void step(float dt) {
      dt = std::clamp(dt, 0.001f, 0.1f);
      time_ += dt;
      angle_ += 0.36f * dt;
      const float follow = 1.0f - std::pow(0.85f, dt * 30.0f);
      bool live[kVoiceCount] = {};
      for (const Loop &l : loops_) {
         Place &p = places_[l.slot];
         live[l.slot] = true;
         // Each axis from its source (by default low notes to the left, high to the right, and the
         // harder a voice blows the higher it floats); alternate voices a little apart, so two
         // notes alike don't sit on each other.
         const float apart = l.slot % 2 ? 1.0f : -1.0f;
         const float tx = std::clamp(0.06f + 0.88f * along(placing.x, l) + (placing.x == kLoopPitch ? 0.0f : 0.03f * apart), 0.0f, 1.0f);
         const float ty = std::clamp(0.82f - 0.6f * along(placing.y, l) + 0.08f * apart, 0.08f, 0.92f);
         if (!p.seen) {
            p = {tx, ty, true};
         } else {
            p.x += (tx - p.x) * follow;
            p.y += (ty - p.y) * follow;
         }
         trace(l, shapes_[l.slot], W_, H_);
      }
      for (int i = 0; i < kVoiceCount; ++i)
         if (!live[i])
            shapes_[i].clear();
      stepVapor(dt, W_, H_);
      redraw();
   }

   void draw(visage::Canvas &c) override {
      const float k = s(), W = W_, H = H_;
      const Colors pal = colors();
      // The vapour paints the whole view, its night-sky ground included, added onto black.
      c.setColor(visage::Color(0xff000000));
      c.roundedRectangle(0, 0, width(), height(), 10 * k);
      drawSmoke(c, k);
      c.setColor(visage::Color(0x22ffffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * k, 1 * k);

      if (full) {
         // The voices sounding, in a box in the corner.
         const float bw = 260, bx = W - bw - 16, by = 16;
         c.setColor(visage::Color(0xcc11151d));
         c.roundedRectangle(bx * k, by * k, bw * k, 62 * k, 6 * k);
         c.setColor(visage::Color(0x22ffffff));
         c.roundedRectangleBorder(bx * k, by * k, bw * k, 62 * k, 6 * k, 1 * k);
         std::string notes;
         int sounding = 0;
         for (const Loop &l : loops_)
            if (!l.releasing) {
               notes += (notes.empty() ? "" : "  ·  ") + ui::noteName(l.note);
               ++sounding;
            }
         char head[48];
         std::snprintf(head, sizeof(head), "%d VOICE%s SOUNDING", sounding, sounding == 1 ? "" : "S");
         label(c, head, bx + 14, by + 10, bw - 28, kDim, 11.0f);
         label(c, notes.empty() ? "—" : notes, bx + 14, by + 32, bw - 28, kText, 11.5f);
      } else {
         char head[64];
         std::snprintf(head, sizeof(head), "%d LOOP%s  ·  %s", static_cast<int>(loops_.size()), loops_.size() == 1 ? "" : "S",
                       kFamilyNames[model_]);
         label(c, head, W - 320, 16, 304, kDim, 11.0f, visage::Font::kRight);
      }
      if (!hint.empty())
         label(c, hint, 16, 46, W - 32, hintColor, 12.0f, visage::Font::kCenter);
      if (loops_.empty()) {
         label(c, "Play a note: each voice draws its breath here", 0, H / 2 - 10, W, kFaint, 13.0f, visage::Font::kCenter);
         return;
      }
      for (const Loop &l : loops_)
         drawLoop(c, l, pal, k);
   }

private:

   struct Place {
      float x = 0.5f, y = 0.5f;
      bool seen = false;
   };
   // A loop as drawn, in design units, newest point last.
   struct Shape {
      std::vector<visage::Point> pts;
      float cx = 0, cy = 0, radius = 0, breath = 0, fade = 1, emit = 0;
      int note = 0;
      void clear() { pts.clear(); }
   };
   // The palette: the warm colour of fresh smoke (or ink), the lavender it cools to, the vapour's blue.
   struct Colors {
      unsigned hot, cold, mist, line;
   };
   Colors colors() const {
      switch (look.palette) {
      case 1: // ember
         return {0xffff6a2a, 0xff8a4a6a, 0xff5a3a6a, 0xffffe2c8};
      case 2: // frost
         return {0xff9fe8ff, 0xff6f8cff, 0xff4a6ad8, 0xfff0fbff};
      case 3: // moss
         return {0xff9be07a, 0xff3f8f7a, 0xff2f6a7a, 0xfff0ffe0};
      case 4: // mono
         return {0xfff0ece4, 0xff7a7f8a, 0xff5a5f6a, 0xffffffff};
      default: // dusk: the instrument's own colour, cooling to lavender, over blue vapour
         return {modelColor(model_), 0xff9c98d8, 0xff5a72d0, 0xfffff4e0};
      }
   }


   // The loop's line: the tone now, a quarter period ago and half a period ago, turned in 3D.
   void trace(const Loop &l, Shape &sh, float W, float H) {
      const Place &p = places_[l.slot];
      sh.cx = 60 + p.x * (W - 120);
      sh.cy = p.y * H;
      sh.breath = l.breath;
      sh.note = l.note;
      sh.fade = l.releasing ? 0.45f : 1.0f;
      const float size = placing.sizeMin + (1.0f - placing.sizeMin) * along(placing.size, l);
      sh.radius = 70 * size * std::min(1.5f, H / 494.0f);
      sh.pts.clear();
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
      const float ca = std::cos(angle_ + l.slot), sa = std::sin(angle_ + l.slot), tilt = 0.3f;
      const float ct = std::cos(tilt), st = std::sin(tilt);
      // About 150 points a loop: smooth at any size, and few enough to draw at 60 a second.
      const int stride = std::max(1, span / 150);
      for (int i = first; i < n; i += stride) {
         const float a = x[i] / peak, b = x[i - tau] / peak, z = x[i - 2 * tau] / peak;
         const float X = a * ca + z * sa, Z = -a * sa + z * ca;
         const float Y = b * ct - Z * st;
         sh.pts.push_back({sh.cx + X * sh.radius, sh.cy - Y * sh.radius});
      }
   }

   // Where a source puts a loop along an axis, 0..1.
   static float along(int source, const Loop &l) {
      switch (source) {
      case kLoopPitch: return std::clamp((l.note - 36) / 52.0f, 0.0f, 1.0f);
      case kLoopStrike: return std::clamp(l.strike, 0.0f, 1.0f);
      case kLoopPress: return std::clamp(l.press, 0.0f, 1.0f);
      case kLoopSlide: return std::clamp(l.slide, 0.0f, 1.0f);
      case kLoopBreath: return std::clamp(l.breath, 0.0f, 1.0f);
      case kLoopRandom: return 0.5f + 0.5f * std::clamp(l.random, -1.0f, 1.0f);
      default: return 0.5f;
      }
   }

   // -1..1, from a xorshift so every platform draws the same smoke.
   float random() {
      rng_ ^= rng_ << 13;
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      return static_cast<float>(rng_ & 0xffffff) / 8388608.0f - 1.0f;
   }

   // The smoke is the mock-up's vapour (vapor.h): every sounding loop breathes out along its whole
   // line as a ring of jets, hardest where its wave's peak is running, so the plume is drawn off the
   // curve; three slow wanderers trail faint cold mist through the lower air whether anything plays
   // or not. The panel's look knobs set how it moves and how long it hangs.
   void layoutField(float W, float H) {
      // The AIR view gets a finer grid than the loops card, as in the mock-up.
      const bool small = W < 900.0f;
      velShort_ = small ? 72 : 96;
      dyeShort_ = small ? 120 : 168;
      (void)H;
   }

   static float smooth01(float x) {
      const float t = std::clamp(x, 0.0f, 1.0f);
      return t * t * (3 - 2 * t);
   }

   // A note's smoke colour: a spot on the palette's three-stop ramp, picked by its pitch class round
   // the circle of fifths, so neighbouring notes never share a colour.
   void noteColour(int note, float glowOut[3], float dyeOut[4]) const {
      using Ramp = std::array<std::array<float, 3>, 3>;
      static constexpr Ramp kFlute = {{{.55f, .86f, 1}, {.36f, .9f, .78f}, {.74f, .64f, 1}}};
      static constexpr Ramp kClarinet = {{{1, .74f, .3f}, {1, .5f, .16f}, {1, .36f, .34f}}};
      static constexpr Ramp kSax = {{{1, .8f, .4f}, {1, .56f, .22f}, {.95f, .4f, .52f}}};
      static constexpr Ramp kOboe = {{{1, .7f, .46f}, {.98f, .48f, .3f}, {.86f, .42f, .62f}}};
      static constexpr Ramp kTrumpet = {{{1, .6f, .18f}, {1, .32f, .16f}, {.9f, .28f, .52f}}};
      static constexpr Ramp kEmber = {{{1, .52f, .16f}, {1, .3f, .1f}, {.92f, .2f, .26f}}};
      static constexpr Ramp kFrost = {{{.6f, .9f, 1}, {.45f, .7f, 1}, {.75f, .66f, 1}}};
      static constexpr Ramp kMoss = {{{.62f, .95f, .45f}, {.36f, .86f, .58f}, {.8f, .9f, .36f}}};
      static constexpr Ramp kMono = {{{1, .97f, .92f}, {.92f, .92f, .95f}, {.86f, .88f, .96f}}};
      static constexpr Ramp kAlien = {{{.62f, 1, .36f}, {.3f, .95f, .7f}, {.72f, .5f, 1}}};
      static constexpr Ramp kFamilies[6] = {kFlute, kClarinet, kSax, kOboe, kTrumpet, kAlien};
      const Ramp &st = look.palette == 1   ? kEmber
                       : look.palette == 2 ? kFrost
                       : look.palette == 3 ? kMoss
                       : look.palette == 4 ? kMono
                                           : kFamilies[std::clamp(model_, 0, 5)];
      const float t = static_cast<float>((((note * 7) % 12) + 12) % 12) / 11.0f;
      const float u = t < 0.5f ? t * 2 : (t - 0.5f) * 2;
      const auto &a = st[t < 0.5f ? 0 : 1], &b = st[t < 0.5f ? 1 : 2];
      float sum = 0;
      for (int i = 0; i < 3; ++i) {
         glowOut[i] = a[i] + (b[i] - a[i]) * u;
         sum += glowOut[i];
      }
      for (int i = 0; i < 3; ++i)
         dyeOut[i] = glowOut[i] / sum;
      dyeOut[3] = 0;
   }

   // A step of the vapour: the jets off every sounding loop, the wanderers, then the solver.
   void stepVapor(float dt, float W, float H) {
      const float now = time_, sc = std::min(W, H) / 480.0f;
      const float rise = look.rise / 0.55f, turb = look.turbulence / 0.42f, swirl = look.swirl / 0.38f;
      const float tail = std::max(0.25f, look.tail / 4.0f), sink = look.ink ? -0.6f : 1.0f;
      // The room's draft, off vertical.
      const float lean = 0.3f + 0.22f * std::sin(now * 0.071f) + 0.1f * std::sin(now * 0.023f + 1.3f);
      emitters_.clear();
      int live = 0;
      for (const Shape &sh : shapes_)
         if (sh.pts.size() > 8 && sh.breath >= 0.008f)
            ++live;
      const int per = live ? std::clamp((48 - 3) / live, 4, 12) : 0;
      for (int slot = 0; slot < kVoiceCount; ++slot) {
         const Shape &sh = shapes_[slot];
         if (sh.pts.size() <= 8 || sh.breath < 0.008f)
            continue;
         const float b = std::min(sh.breath, 1.35f), fold = smooth01((sh.breath - 1.0f) / 0.25f);
         const float jitter = 1.7f * slot + 0.37f * sh.note;
         float flutter = 1.0f + fold * (0.5f * std::sin(now * 13.1f + jitter) + 0.3f * std::sin(now * 29.3f + jitter * 2.1f));
         flutter *= 0.7f + 0.3f * turb;
         const float total = (0.55f + 0.9f * b) * (1.0f + fold * 0.5f) * sh.fade;
         // Sample points slide along the curve, so the whole line feeds the vapour; the peak of
         // the wave (the bead) travels round it and breathes hardest.
         const float offs = std::fmod(now * 0.37f + jitter, 1.0f), bead = std::fmod(now * 0.61f + 0.29f * slot, 1.0f);
         const int n = static_cast<int>(sh.pts.size()) - 1;
         float ws[12];
         int at[12];
         float wsum = 0;
         for (int k = 0; k < per; ++k) {
            const float f = std::fmod(static_cast<float>(k) / per + offs, 1.0f);
            at[k] = std::min(n - 2, static_cast<int>(f * n));
            float d = std::fabs(f - bead);
            d = std::min(d, 1.0f - d);
            ws[k] = 0.35f + 1.6f * std::exp(-d * d / 0.006f);
            wsum += ws[k];
         }
         float glow[3], dye[4];
         noteColour(sh.note, glow, dye);
         for (int k = 0; k < per; ++k) {
            const visage::Point p = sh.pts[static_cast<size_t>(at[k])], p2 = sh.pts[static_cast<size_t>(at[k] + 2)];
            float tx = p2.x - p.x, ty = p2.y - p.y;
            const float tl = std::hypot(tx, ty) + 1e-6f;
            tx /= tl;
            ty /= tl;
            float nx = p.x - sh.cx, ny = p.y - sh.cy;
            const float nl = std::hypot(nx, ny) + 1e-6f;
            nx /= nl;
            ny /= nl;
            const float w = ws[k] / wsum * per;
            const float ts = 55 * sc * (0.5f + b), ns = (35 + 70 * b) * sc * flutter * (0.6f + 0.4f * w);
            // Steam rises off the curve, leaning with the draft; ink sinks.
            const float up = (40 + 50 * b) * sc * rise * sink;
            const float vx = tx * ts + nx * ns + std::sin(lean) * up, vy = ty * ts + ny * ns - std::cos(lean) * up;
            VaporEmitter e;
            e.x = p.x / W;
            e.y = 1.0f - p.y / H;
            e.r = (3 * sc + sh.radius * 0.05f * (0.7f + 0.3f * w)) / H;
            e.vx = vx / H;
            e.vy = -vy / H;
            e.grip = 4 + 6 * b;
            e.dye = total * 3.4f * ws[k] / wsum;
            std::copy(dye, dye + 4, e.col);
            emitters_.push_back(e);
         }
      }
      // The room breathes on its own: three slow wanderers trail faint cold mist through the lower
      // air (the upper, for ink), which the current and the curls pull into wisps.
      for (int i = 0; i < 3; ++i) {
         const float ax = 0.023f + 0.011f * i, ay = 0.037f + 0.009f * i, px = i * 2.1f, py = i * 1.3f;
         const float x = 0.5f + 0.44f * std::sin(now * ax + px), y = 0.1f + 0.3f * (0.5f + 0.5f * std::sin(now * ay + py));
         const float vx = 0.44f * ax * std::cos(now * ax + px), vy = 0.15f * ay * std::cos(now * ay + py);
         const float m = std::hypot(vx, vy) + 1e-6f;
         VaporEmitter e;
         e.x = x;
         e.y = look.ink ? 1.0f - y : y;
         e.r = 0.075f;
         e.vx = 0.06f * vx / m;
         e.vy = 0.06f * vy / m * sink;
         e.swirl = 0.012f * (i - 1);
         e.grip = 0.6f;
         e.dye = 0.2f * (0.6f + 0.4f * std::sin(now * (0.11f + 0.03f * i) + i * 1.7f));
         e.col[3] = 1;
         emitters_.push_back(e);
      }
      VaporRunner::Job job;
      job.w = W;
      job.h = H;
      job.velShort = velShort_;
      job.dyeShort = dyeShort_;
      job.dt = dt;
      job.time = now;
      job.glow = look.glow / 0.5f;
      job.ink = look.ink != 0;
      job.emitters = emitters_;
      for (const Shape &sh : shapes_) {
         if (sh.pts.size() <= 8)
            continue;
         const float a = std::min(1.0f, sh.breath * 1.6f) * sh.fade;
         if (a < 0.02f)
            continue;
         VaporLight L;
         float dye[4];
         noteColour(sh.note, L.col, dye);
         L.x = sh.cx / W;
         L.y = 1.0f - sh.cy / H;
         L.r = sh.radius * 1.5f / H;
         L.k = 0.6f * a;
         job.lights.push_back(L);
      }
      VaporParams &P = job.params;
      P.rise = 0.03f * rise * sink;
      P.buoy = 0.14f * rise * sink;
      P.drift = 0.016f * turb;
      P.curl = 11.0f * std::max(0.0f, swirl);
      P.iters = W < 900.0f ? 14 : 18;
      for (float &r : P.rates)
         r /= tail;
      runner_->submit(std::move(job));
   }

   void drawSmoke(visage::Canvas &c, float k) {
      bool ready = false;
      runner_->latest([&](int dw, int dh, const std::vector<float> &red, const std::vector<float> &green, const std::vector<float> &blue) {
         ready = true;
         if (heat_[0].width() != dw || heat_[0].height() != dh)
            for (auto &h : heat_)
               h.setDimensions(dw, dh);
         const std::vector<float> *rgb[3] = {&red, &green, &blue};
      // Into three heat maps (red, green, blue), rows top down, clear of the view's rounded corners.
      const float cellW = W_ / dw, cellH = H_ / dh, R = 10.0f;
      for (int j = 0; j < dh; ++j) {
         const float y = (dh - 1 - j + 0.5f) * cellH;
         for (int i = 0; i < dw; ++i) {
            const float x = (i + 0.5f) * cellW;
            const float qx = std::max(0.0f, std::max(R - x, x - (W_ - R))), qy = std::max(0.0f, std::max(R - y, y - (H_ - R)));
            const float inside = std::clamp(R + 0.5f * cellW - std::hypot(qx, qy), 0.0f, cellW) / cellW;
            const size_t n = static_cast<size_t>(j) * dw + i;
            for (int ch = 0; ch < 3; ++ch)
               heat_[ch].at(i, dh - 1 - j) = (*rgb[ch])[n] * inside;
         }
      }
      });
      if (!ready)
         return;
      static const visage::Color kChannel[3] = {visage::Color(0xffff0000), visage::Color(0xff00ff00), visage::Color(0xff0000ff)};
      c.setBlendMode(visage::BlendMode::Add);
      for (int ch = 0; ch < 3; ++ch) {
         c.setColor(visage::Brush::horizontal(visage::Gradient(visage::Color(0xff000000), kChannel[ch])));
         c.heatMap(heat_[ch], 0.0f, 0.0f, W_ * k, H_ * k);
      }
      c.setBlendMode(visage::BlendMode::Alpha);
   }

   void drawLoop(visage::Canvas &c, const Loop &l, const Colors &pal, float k) {
      const Shape &sh = shapes_[l.slot];
      if (sh.pts.size() < 2)
         return;
      std::vector<visage::Point> pts;
      pts.reserve(sh.pts.size());
      for (const visage::Point &q : sh.pts)
         pts.push_back({q.x * k, q.y * k});
      const float fade = sh.fade, glowAmount = look.glow / 0.5f, width = 0.5f + 1.25f * look.line;
      // The glow is wide and soft, so every other point is enough for it.
      if (!look.ink && glowAmount > 0.01f)
         for (size_t i = 2; i < pts.size(); i += 2) {
            // Older parts of the trail fainter.
            const float age = static_cast<float>(i) / pts.size();
            c.setColor(glow(pal.hot, 0.07f * fade * age * glowAmount));
            c.segment(pts[i - 2].x, pts[i - 2].y, pts[i].x, pts[i].y, 10 * k * width, true);
            c.setColor(glow(pal.hot, 0.16f * fade * age * glowAmount));
            c.segment(pts[i - 2].x, pts[i - 2].y, pts[i].x, pts[i].y, 4.5f * k * width, true);
         }
      for (size_t i = 1; i < pts.size(); ++i) {
         const float age = static_cast<float>(i) / pts.size();
         if (look.ink)
            c.setColor(ui::steady(visage::Color(pal.hot).withAlpha((0.3f + 0.7f * age) * fade)));
         else
            c.setColor(glow(pal.line, (0.25f + 0.7f * age) * fade, 1.0f + 0.6f * age));
         c.segment(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, 1.7f * k * width, true);
      }
      char t[48];
      std::snprintf(t, sizeof(t), "%s   %.0f%%", ui::noteName(l.note).c_str(), 100.0f * std::min(1.0f, l.breath / 1.3f));
      label(c, t, sh.cx - 60, sh.cy + sh.radius + 10, 120, kFaint, 10.5f, visage::Font::kCenter);
   }

   std::vector<Loop> loops_;
   Place places_[kVoiceCount];
   Shape shapes_[kVoiceCount];
   // The smoke solves on its own thread, except in screenshots, which need the same picture every time.
   std::unique_ptr<VaporRunner> runner_ = std::make_unique<VaporRunner>(!gSyncSmoke);
   std::vector<VaporEmitter> emitters_;
   int velShort_ = 72, dyeShort_ = 120;
   visage::HeatMapData heat_[3];
   uint32_t rng_ = 20251107u;
   int model_ = kClarinet;
   double rate_ = 48000.0;
   float W_, H_;
   float angle_ = 0.0f, time_ = 0.0f;
};

// ------------------------------------------------------------------ MAP

unsigned sourceColor(int source) { return kModSourceColors[std::clamp(source, 0, kModSourceCount - 1)]; }
std::string upper(std::string t) {
   for (char &ch : t)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
   return t;
}

// What MAP mode needs from the panel. Knobs ask it for their rings and hand it their drags.
class Mapper {
public:
   virtual ~Mapper() = default;
   virtual bool mapping() const = 0;
   // The source MAP mode draws routes from, or kSrcOff.
   virtual int armedSource() const = 0;
   struct Ring {
      int route, source;
      double amount;
   };
   // The routes onto destination `dest`, in route order.
   virtual void rings(int dest, std::vector<Ring> &out) const = 0;
   // The route from the armed source to `dest`, made if there is none (-1 when all are taken).
   virtual int routeFor(int dest) = 0;
   virtual double routeAmount(int route) const = 0;
   virtual void beginRoute(int route) = 0;
   virtual void setRouteAmount(int route, double amount) = 0;
   // Ends the drag; a route left at zero is removed.
   virtual void endRoute(int route) = 0;
   // Removes the armed source's routes to `dest`, or every route to it with nothing armed.
   virtual void unmap(int dest) = 0;
};

// A knob MAP mode can map. With a source armed, dragging it sets that route's amount instead of the
// knob's value, a double-click flips the route's sign and a right-click removes it. In MAP with a
// source armed it shows the route as a ring outside its own, in the source's colour, spanning what
// the route adds.
class MapKnob : public ui::Knob {
public:
   MapKnob(ui::ValueRange range, Mapper &mapper, clap_id id) : Knob(range), mapper_(mapper), dest_(destinationOf(id)) {}

   int destination() const { return dest_; }

   void draw(visage::Canvas &c) override {
      Knob::draw(c);
      const int armed = mapper_.armedSource();
      if (dest_ < 0 || !mapper_.mapping() || armed == kSrcOff)
         return;
      rings_.clear();
      mapper_.rings(dest_, rings_);
      const float w = width(), h = height(), s = h / 100.0f;
      const float d = std::min(w * 0.7f, 48.0f * s);
      const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;
      const float rd = d + 13.0f * s, sweep = 0.75f * kPiF;
      const float from = fraction(value());
      c.setColor(glow(sourceColor(armed), 0.55f));
      c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 1.4f * s, -0.5f * kPiF, sweep, true);
      for (const Mapper::Ring &r : rings_) {
         if (r.source != armed)
            continue;
         const float to = std::clamp(from + static_cast<float>(r.amount), 0.0f, 1.0f);
         if (std::fabs(to - from) > 0.002f) {
            const float a0 = -sweep + 2.0f * sweep * from, a1 = -sweep + 2.0f * sweep * to;
            c.setColor(glow(sourceColor(armed), 1.0f, 1.6f));
            c.arc(cx - rd * 0.5f, cy - rd * 0.5f, rd, 3.4f * s, 0.5f * (a0 + a1) - 0.5f * kPiF, 0.5f * std::fabs(a1 - a0), true);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (!mapper_.mapping() || dest_ < 0)
         return Knob::mouseDown(e);
      if (e.isRightButton()) {
         mapper_.unmap(dest_);
         redraw();
         return;
      }
      if (mapper_.armedSource() == kSrcOff)
         return;
      route_ = mapper_.routeFor(dest_);
      if (route_ < 0)
         return;
      mapper_.beginRoute(route_);
      start_ = mapper_.routeAmount(route_);
      if (e.repeatClickCount() == 2)
         mapper_.setRouteAmount(route_, start_ = -start_);
      startY_ = e.position.y;
      redraw();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return Knob::mouseDrag(e);
      mapper_.setRouteAmount(route_, std::clamp(start_ + (startY_ - e.position.y) / (2.0f * height()), -1.0, 1.0));
      redraw();
   }
   void mouseUp(const visage::MouseEvent &e) override {
      if (route_ < 0)
         return Knob::mouseUp(e);
      mapper_.endRoute(route_);
      route_ = -1;
      redraw();
   }

private:
   Mapper &mapper_;
   int dest_;
   int route_ = -1;
   double start_ = 0.0;
   float startY_ = 0.0f;
   std::vector<Mapper::Ring> rings_;
};

// A modulation source you can touch: a pill with its name, filled as far as the source is now. In
// MAP mode it pulses in its colour; armed, it is lit.
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
   // How far the source is now, 0..1; a bipolar one passes -1..1 as 0..1.
   void setLevel(float level, bool bipolar) {
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
      const float w = width(), h = height(), s = h / 26.0f, r = h * 0.5f;
      const unsigned color = sourceColor(source_);
      c.setColor(visage::Color(armed_ ? 0xe0161a26 : 0xa0101420));
      c.roundedRectangle(0, 0, w, h, r);
      if (armed_) {
         c.setColor(glow(color, 0.3f));
         c.roundedRectangle(0, 0, w, h, r);
      }
      if (level_ > 0.0f) {
         c.setColor(glow(color, armed_ ? 0.35f : 0.2f));
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
      const float lx = 12 * s, ly = h * 0.5f, halo = 10 * s;
      c.setColor(visage::Brush::radial(glow(color, 0.7f * led_), glow(color, 0.0f), visage::Point(lx, ly), halo));
      c.circle(lx - halo, ly - halo, 2 * halo);
      c.setColor(visage::Color(color).interpolateWith(0xff20232c, 0.6f * (1.0f - led_)));
      c.circle(lx - 4.0f * s, ly - 4.0f * s, 8 * s);
      c.setColor(armed_ ? 0xffffffff : kText);
      c.text(label_, ui::font(12 * s), visage::Font::kLeft, 22 * s, 0, w - 24 * s, h);
   }

private:
   int source_;
   std::string label_;
   bool mapping_ = false, armed_ = false;
   float pulse_ = 0.0f, level_ = 0.0f, led_ = 0.0f;
};

// An LFO's or the Swell's contour, with a dot where the newest voice is on it.
class GeneratorGraph : public Drawn {
public:
   GeneratorGraph(float w, int source) : Drawn(w), source_(source) {}
   void set(int shape, double value) {
      if (shape != shape_ || std::fabs(value - value_) > 0.003) {
         shape_ = shape;
         value_ = value;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      well(c);
      const float k = s(), W = width() / k, H = height() / k;
      const unsigned color = sourceColor(source_);
      const bool swell = source_ == kSrcSwell;
      const auto y = [&](double t) {
         if (swell) {
            switch (shape_) {
            case kSwellRise:
               return t * t * (3 - 2 * t);
            case kSwellFall:
               return 1.0 - t * t * (3 - 2 * t);
            case kSwellWave:
               return 0.5 - 0.5 * std::cos(4 * kPi * t);
            case kSwellBloom:
               return t < 0.15 ? t / 0.15 : 1.0 - 0.6 * (t - 0.15) / 0.85;
            case kSwellTide:
               return 0.5 - 0.5 * std::cos(2 * kPi * t * t);
            default:
               return std::sin(kPi * t);
            }
         }
         const double x = std::fmod(2.0 * t, 1.0);
         // S&H and Smooth drawn from a fixed row of steps.
         static const double steps[] = {0.3, -0.6, 0.8, -0.1, 0.5, -0.9, 0.2, 0.7};
         const int i = static_cast<int>(t * 8.0) % 8;
         switch (shape_) {
         case kLfoTriangle:
            return 0.5 + 0.5 * (1.0 - 4.0 * std::fabs(x - 0.5));
         case kLfoSaw:
            return x;
         case kLfoSquare:
            return x < 0.5 ? 1.0 : 0.0;
         case kLfoSampleHold:
            return 0.5 + 0.5 * steps[i];
         case kLfoSmooth: {
            const double f = std::fmod(t * 8.0, 1.0), e = f * f * (3 - 2 * f);
            return 0.5 + 0.5 * (steps[(i + 7) % 8] + (steps[i] - steps[(i + 7) % 8]) * e);
         }
         default:
            return 0.5 + 0.5 * std::sin(2 * kPi * x);
         }
      };
      const float x0 = 10, x1 = W - 10, y0 = H - 12, y1 = 14;
      visage::Path p;
      constexpr int n = 96;
      for (int i = 0; i <= n; ++i) {
         const double t = static_cast<double>(i) / n;
         const float px = (x0 + (x1 - x0) * t) * k, py = (y0 + (y1 - y0) * y(t)) * k;
         if (i == 0)
            p.moveTo(px, py);
         else
            p.lineTo(px, py);
      }
      c.setColor(glow(color, 0.3f));
      c.fill(p.stroke(4 * k));
      c.setColor(glow(color, 0.95f, 1.2f));
      c.fill(p.stroke(1.6f * k));
      // Where the newest voice is: a level line across.
      const double v = bipolarSource(source_) ? 0.5 + 0.5 * value_ : value_;
      const float ly = (y0 + (y1 - y0) * std::clamp(v, 0.0, 1.0)) * k;
      c.setColor(glow(0xffffffff, 0.25f));
      c.fill(x0 * k, ly - 0.5f * k, (x1 - x0) * k, 1.0f * k);
   }

private:
   int source_;
   int shape_ = -1;
   double value_ = 0.0;
};

// A route in the ROUTES list: its source and destination, the amount as a bar you drag, and a
// cross that removes it.
class RouteAmount : public Drawn {
public:
   RouteAmount(float w, EditorHost &host, int route) : Drawn(w), host_(host), route_(route) {}
   void draw(visage::Canvas &c) override {
      const float k = s(), W = width() / k, H = height() / k;
      const double a = host_.paramValue(routeParam(route_, kRouteAmount));
      const int src = static_cast<int>(host_.paramValue(routeParam(route_, kRouteSource)) + 0.5);
      const unsigned color = sourceColor(src);
      const float mid = W * 0.5f, y = H * 0.5f;
      c.setColor(visage::Color(0x22ffffff));
      c.roundedRectangle(0, (y - 1.5f) * k, W * k, 3 * k, 1.5f * k);
      const float to = mid + static_cast<float>(a) * (W * 0.5f - 6);
      c.setColor(glow(color, 0.9f, 1.3f));
      c.roundedRectangle(std::min(mid, to) * k, (y - 2) * k, std::fabs(to - mid) * k, 4 * k, 2 * k);
      c.setColor(visage::Color(0x55ffffff));
      c.fill((mid - 0.5f) * k, (y - 5) * k, 1 * k, 10 * k);
      c.setColor(kText);
      c.circle((to - 5) * k, (y - 5) * k, 10 * k);
      char t[16];
      std::snprintf(t, sizeof(t), "%+.0f%%", a * 100.0);
      label(c, t, W - 60, -2, 60, kDim, 10.0f, visage::Font::kRight);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      host_.beginEdit(routeParam(route_, kRouteAmount));
      if (e.repeatClickCount() == 2) {
         host_.setParam(routeParam(route_, kRouteAmount), -host_.paramValue(routeParam(route_, kRouteAmount)));
         redraw();
         return;
      }
      drag(e);
   }
   void mouseDrag(const visage::MouseEvent &e) override { drag(e); }
   void mouseUp(const visage::MouseEvent &) override { host_.endEdit(routeParam(route_, kRouteAmount)); }

private:
   void drag(const visage::MouseEvent &e) {
      const float k = s(), W = width() / k;
      const double a = std::clamp((e.position.x / k - W * 0.5f) / (W * 0.5f - 6), -1.0f, 1.0f);
      host_.setParam(routeParam(route_, kRouteAmount), std::round(a * 100.0) / 100.0);
      redraw();
   }
   EditorHost &host_;
   int route_;
};

// ------------------------------------------------------------------ the panel

// The panel behind the AIR view's knobs.
class AirBackdrop : public Drawn {
public:
   explicit AirBackdrop(float w) : Drawn(w) {}
   void draw(visage::Canvas &c) override {
      const float k = s();
      c.setColor(visage::Color(0xd010141c));
      c.roundedRectangle(0, 0, width(), height(), 8 * k);
      c.setColor(visage::Color(0x26ffffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 8 * k, 1 * k);
      label(c, "VAPOR", 16, 10, 200, kDim, 11.0f);
      label(c, "LOOPS", 16 + 4 * 92, 10, 200, kDim, 11.0f);
      label(c, "PLACE", 584, 10, 200, kDim, 11.0f);
      label(c, "ACROSS", 584, 40, 60, kDim, 10.0f);
      label(c, "UP", 584, 74, 60, kDim, 10.0f);
      label(c, "SIZE", 584, 108, 60, kDim, 10.0f);
   }
};

// The dice card behind RANDOMIZE, MUTATE and the locks: one of the panel's cards, a little darker
// and edged in the accent, so it reads as floating over the breath loops.
class DiceCard : public Drawn {
public:
   explicit DiceCard(float w) : Drawn(w) {}
   void draw(visage::Canvas &c) override {
      const float k = s();
      c.setColor(visage::Color(0xf4161a21));
      c.roundedRectangle(0, 0, width(), height(), 10 * k);
      c.setColor(visage::Color(0x80f28c28));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * k, 1 * k);
      c.setColor(0xfff28c28);
      ui::drawDie(c, 24 * k, 21 * k, 14 * k);
      label(c, "DICE", 40, 11, 120, 0xfff28c28, 15.0f);
      c.setColor(kCardBorder);
      c.fill(14 * k, 108 * k, width() - 28 * k, 1 * k);
      label(c, "LOCK", 14, 116, 200, kDim, 10.0f);
      label(c, "locked sections stay as they are", 60, 117, width() / k - 74, kFaint, 9.0f, visage::Font::kRight);
   }
};

class Panel : public ui::DesignFrame, public visage::EventTimer, public Mapper {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      auto &notice = add<ui::UpdateNotice>({1372, 13, 120, 26});
      // The name in the header shows this plugin's version on hover.
      tips_.add(add<ui::HoverArea>({8, 6, 164, 40}), "JBreath version " JCLAP_PLUGIN_VERSION);
      auto &presets = add<ui::PresetBar>({416, 11, 340, 30}, factoryPresets(), kPresetFormat, host_);
      presets.setAccent(0xfff28c28);
      presets.defaults = defaultValues();
      // A preset sets the sound; the AIR view's look stays as it is.
      presets.onLoad = [this](const ui::Preset &p) {
         const std::vector<double> d = defaultValues();
         std::vector<double> values = d;
         for (const auto &[id, v] : p.values)
            if (id < values.size())
               values[id] = v;
         for (clap_id id = 0; id < values.size(); ++id) {
            if (isLookParam(id) || host_.paramValue(id) == values[id])
               continue;
            host_.beginEdit(id);
            host_.setParam(id, values[id]);
            host_.endEdit(id);
         }
      };
      presets.currentName = [this] { return host_.presetName(); };
      presets.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      // Its die opens the dice card, which RANDOMIZEs a new patch or MUTATEs the one there is.
      presets.onDice = [this] { showDice(!host_.diceOpen); };
      presets.diceLit = [this] { return host_.diceOpen; };
      presetBar_ = &presets;
      (void)notice;
      // Everything from here to the loops belongs to the PLAY view.
      const size_t first = children().size();
      buildExciter();
      buildBore();
      buildPlayer();
      buildVoice();
      for (size_t i = first; i < children().size(); ++i)
         playFrames_.push_back(children()[i]);
      loops_ = &add<BreathLoops>(kLoopsRect, kLoopsRect.w, kLoopsRect.h);
      tips_.add(*loops_, "Each voice as a loop: its sound now, a quarter period ago and half a period ago. "
                         "A steady tone draws a clean loop; growl and breath fray it. Smoke rises off it as it blows.");
      buildMap();
      buildRoutes();
      buildAir();
      // Last, so it floats over the loops.
      buildDice();
      showDice(host_.diceOpen);
      frame(1.0f / 60.0f);
      startTimer(1);
   }

   void showDice(bool open) {
      host_.diceOpen = open;
      presetBar_->redraw();
      for (visage::Frame *f : dice_)
         f->setVisible(open && !air_);
      refreshDice();
   }

   void open(const ScreenshotOptions &o) {
      playerTabs_->select(o.playerTab);
      exciterTabs_->select(o.exciterTab);
      voiceTabs_->select(o.voiceTab);
      if (o.air)
         setAir(true);
      if (o.routes) {
         view_->setSelected(1);
         showRoutes(true);
      }
      if (o.armed > 0)
         setMapping(true, o.armed);
      if (o.dice)
         showDice(true);
   }

   // The timer is checked at every screen refresh; the pacer says which refreshes step the
   // animation, and by how much time.
   void timerCallback() override {
      if (const float dt = pacer_.tick())
         frame(dt);
   }

   // One frame: the voices read once, the loops and smoke stepped by dt, and the rest of the panel
   // brought up to date at half the rate (none of it moves fast enough to need more). The smoke
   // lives on a fixed grid, so its cost doesn't grow with how much of it there is.
   void frame(float dt) {
      ++frameCount_;
      fpsTime_ += dt;
      if (fpsTime_ >= 1.0f) {
         fps_ = frameCount_ / fpsTime_;
         frameCount_ = 0;
         fpsTime_ = 0.0f;
      }

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
         l.strike = v.sources[kSrcStrike];
         l.press = v.pressure;
         l.slide = v.timbre;
         l.random = v.sources[kSrcRandom];
         host_.readVoice(i, l.samples.data(), l.samples.size());
         if (!v.releasing) {
            breaths.push_back(v.breath);
            pressures.push_back(std::clamp(v.pressure, 0.0f, 1.0f));
         }
         if (loudest < 0 || v.level > voices_[loudest].level)
            loudest = i;
         loops.push_back(std::move(l));
      }
      BreathLoops::Look look;
      look.ink = static_cast<int>(host_.paramValue(kLook) + 0.5);
      look.palette = static_cast<int>(host_.paramValue(kPalette) + 0.5);
      look.turbulence = static_cast<float>(host_.paramValue(kTurbulence));
      look.rise = static_cast<float>(host_.paramValue(kRise));
      look.swirl = static_cast<float>(host_.paramValue(kSwirl));
      look.tail = static_cast<float>(host_.paramValue(kTail));
      look.line = static_cast<float>(host_.paramValue(kLine));
      look.glow = static_cast<float>(host_.paramValue(kGlow));
      loops_->look = look;
      BreathLoops::Placing placing;
      placing.x = static_cast<int>(host_.paramValue(kLoopX) + 0.5);
      placing.y = static_cast<int>(host_.paramValue(kLoopY) + 0.5);
      placing.size = static_cast<int>(host_.paramValue(kLoopSize) + 0.5);
      placing.sizeMin = static_cast<float>(host_.paramValue(kLoopSizeMin));
      loops_->placing = placing;

      // The rest of the panel every other frame, and only what's showing.
      if ((tick_++ & 1) == 0 && !air_) {
         // The loudest voice's partials, by a Goertzel filter at each harmonic over its newest samples.
         std::vector<float> partials;
         double hz = 440.0 * std::pow(2.0, (62 - 69) / 12.0);
         if (loudest >= 0) {
            hz = voices_[loudest].hz;
            for (const auto &l : loops)
               if (l.slot == loudest)
                  partials = harmonics(l.samples, hz, rate);
         }
         if (exciterTabs_->selected() == 0)
            valve_->set(model, host_.paramModulated(kEmbouchure), breaths, host_.paramModulated(kStiffness),
                        host_.paramModulated(kAperture), host_.paramModulated(kEdge));
         else if (exciterTabs_->selected() == 2) {
            // The Alien's throat: each voice's place on the ring, and the loudest one's resonances.
            const bool alien = model == kAlien;
            std::vector<float> throats;
            for (const EditorHost::Voice &v : voices_)
               if (v.note >= 0 && !v.releasing && v.throat >= 0.0f)
                  throats.push_back(v.throat);
            const double voice = host_.paramModulated(kAlienVoice);
            throatRing_->set(voice, throats, alien);
            const double pos = loudest >= 0 && voices_[loudest].throat >= 0.0f ? voices_[loudest].throat : voice * kAlienShapes;
            formantGraph_->set(pos, host_.paramModulated(kAlienShift), host_.paramModulated(kAlienSharp),
                               loudest >= 0 ? hz : 0.0, alien);
            for (ui::Knob *kn : alienKnobs_)
               kn->setAccent(alien ? modelColor(kAlien) : kFaint);
         } else {
            // Where each voice's breath has the lowpass now (as the voice works it out).
            const ModelInfo &info = modelInfo(model);
            const double full = std::max(info.breathOffset + info.breathScale * host_.paramModulated(kBreath), 0.1);
            const double amount = host_.paramModulated(kHybridFilterBreath);
            std::vector<float> opens;
            for (float b : breaths)
               opens.push_back(static_cast<float>(std::pow(2.0, amount * 6.0 * (std::clamp(b / full, 0.0, 1.5) - 0.7))));
            filterGraph_->set(host_.paramModulated(kHybridCutoff), host_.paramModulated(kHybridResonance), amount, opens);
         }
         bore_->set(model, hz, loudest >= 0, partials, host_.paramModulated(kLoss), host_.paramModulated(kHoleCutoff) / 1300.0,
                    host_.paramModulated(kCone));
         const bool cone = model == kSax || model == kOboe || model == kTrumpet || model == kAlien, brass = model == kTrumpet;
         coneKnob_->setAccent(cone ? color : kFaint);
         brassKnob_->setAccent(brass ? color : kFaint);
         // The Time knob only sets the delay while Sync is Free.
         delayTimeKnob_->setAccent(host_.paramValue(kDelaySync) < 0.5 ? delayAccent_ : kFaint);
         if (voiceTabs_->selected() == 1)
            curve_->set(host_.paramModulated(kCurve), pressures);
         if (voiceTabs_->selected() == 0) {
            lights_->set(voices_, model, static_cast<int>(host_.paramValue(kVoiceMode) + 0.5));
            badge_->set(model, rate);
            // In: the breath blowing (the loudest voice's), or Breath In when nothing plays.
            float in = static_cast<float>(host_.paramModulated(kBreathIn));
            for (float b : breaths)
               in = std::max(in, std::min(1.0f, b / 1.3f));
            inMeter_->setLevel(in);
            inMeter_->redraw();
            outMeters_[0]->setLevel(host_.outputPeak(0));
            outMeters_[1]->setLevel(host_.outputPeak(1));
            outMeters_[0]->redraw();
            outMeters_[1]->redraw();
         }
         if (playerTabs_->selected() == 0)
            tongue_->set(host_.paramModulated(kAttack), host_.paramModulated(kKick), host_.paramModulated(kRelease), color);
         if (playerTabs_->selected() == 1)
            vibrato_->set(host_.paramModulated(kVibRate), host_.paramModulated(kVibDepth), host_.paramModulated(kVibPitch),
                          host_.paramModulated(kVibDelay), color);
         updateMap();
      } else if (air_) {
         updateChips();
      }
      loops_->set(std::move(loops), model, rate);
      loops_->step(dt);
   }

   // Frames per second over the last second, and the size of the smoke's grid: for the rendering check.
   float fps() const { return fps_; }
   int smokeCells() const { return loops_->cells(); }

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

      if (air_)
         return;
      card(c, k, {16, 64, 880, 332}, "", color);
      card(c, k, {16, 406, 880, 316}, "BORE", color);
      card(c, k, {16, 732, 880, 196}, "", color);
      card(c, k, {904, 612, 728, 316}, "", color);
      if (routes_) {
         c.setColor(0xff141820);
         c.roundedRectangle(912 * k, 64 * k, 720 * k, 494 * k, 10 * k);
         c.setColor(kCardBorder);
         c.roundedRectangleBorder(912 * k, 64 * k, 720 * k, 494 * k, 10 * k, 1 * k);
      }
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
      auto &k = add<MapKnob>(cell, rangeOf(id), *this, id);
      if (k.destination() >= 0)
         mapKnobs_.push_back(&k);
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
      exciterTabs_ = &add<ui::TabBar>({28, 68, 360, 28}, std::vector<std::string>{"EXCITER", "HYBRID", "ALIEN"});
      exciterTabs_->setAccent(0xfff28c28);
      auto &tabs = *exciterTabs_;
      list_ = &tabs.addToPage(0, add<InstrumentList>({32, 104, 190, 276}, 190.0f, host_));
      tips_.add(*list_, "The instrument: how the air is set vibrating, and the tube it plays into.");
      valve_ = &tabs.addToPage(0, add<ValveGraph>({236, 104, 306, 262}, 306.0f));
      tips_.add(*valve_, "The exciter's valve: how the air through it answers the pressure across it. "
                         "Each dot is a sounding voice.");
      const auto top = ui::row({556, 96, 326, 120}, 4), bottom = ui::row({556, 222, 326, 120}, 4);
      tabs.addToPage(0, familyKnob(kBreath, "BREATH", top[0], "How hard the player blows at full pressure (or velocity)."));
      tabs.addToPage(0, familyKnob(kEmbouchure, "EMBOUCHURE", top[1],
                                   "Lip and jaw: the jet's angle on the flute, the bite on a reed, the lips' tension on brass. "
                                   "MPE slide moves it."));
      tabs.addToPage(0, familyKnob(kStiffness, "STIFFNESS", top[2],
                                   "How stiff the reed or lips are (the jet's gain on a flute): stiff is bright and quick to "
                                   "shut, soft is dark and lazy. On the Alien, the bow hair's tension: taut grips and bites."));
      tabs.addToPage(0, familyKnob(kEdge, "BITE", top[3],
                                   "The forte bite: the flute's 2nd harmonic, the reeds' edge, the brass wave steepening into its blare."));
      tabs.addToPage(0, familyKnob(kAperture, "APERTURE", bottom[0],
                                   "How far the reed or lips stand open (the jet's width on a flute): wide is loud and free, "
                                   "narrow is thin and focused. On the Alien, how wide the bow lies on the string."));
      tabs.addToPage(0, familyKnob(kNoise, "NOISE", bottom[1], "Breath noise and turbulence."));
      tabs.addToPage(0, familyKnob(kKick, "KICK", bottom[2], "The burst of pressure as the tongue lets the air go."));
      tabs.addToPage(0, familyKnob(kGrowl, "GROWL", bottom[3],
                                   "A growl in the throat: a rough flutter rides on the air, on any instrument, and a hard-blown "
                                   "reed rasps more."));
      rangeText_ = &tabs.addToPage(0, add<ui::Label>({236, 370, 646, 16}, rangeText(modelOf(host_)), kFaint,
                                                     visage::Font::kLeft, 0.68f));

      // HYBRID: a plain subtractive voice, blown with the same breath.
      auto &hlist = tabs.addToPage(1, add<InstrumentList>({32, 104, 190, 276}, 190.0f, host_, kHybridModel));
      tips_.add(hlist, "HYBRID: a plain oscillator through a lowpass, blown with the same breath and layered with the instrument.");
      filterGraph_ = &tabs.addToPage(1, add<FilterGraph>({236, 104, 306, 262}, 306.0f));
      tips_.add(*filterGraph_, "The lowpass, and (faint) where the softest and hardest breath move it. Each dot is a sounding voice.");
      // Three rows: the oscillator, its lowpass, and its envelope.
      const auto h1 = ui::row({556, 96, 326, 84}, 5), h2 = ui::row({556, 182, 326, 84}, 5),
                 h3 = ui::row({556, 268, 326, 84}, 5);
      const auto synthKnob = [&](clap_id id, const char *name, ui::Rect cell, const char *tip) {
         ui::Knob &k = knob(id, name, kSynthColor, cell, tip);
         k.setTextSizes(8.5f, 9.5f, 8.5f);
         tabs.addToPage(1, k);
      };
      synthKnob(kHybridMix, "MIX", h1[0], "How much of the oscillator: 0 is only the instrument, 100% only the oscillator.");
      synthKnob(kHybridOctave, "OCTAVE", h1[1], "Where the oscillator plays, in octaves from the note.");
      synthKnob(kHybridDetune, "DETUNE", h1[2], "The oscillator's detune, in cents: a little beats against the instrument.");
      synthKnob(kHybridBreath, "BREATH", h1[3], "How strongly the oscillator's level follows the breath: higher swells sooner.");
      synthKnob(kHybridFollow, "FOLLOW", h1[4],
                "How much the oscillator's level follows the breath. 0 is a pad: it plays at the note's velocity, "
                "shaped only by its envelope, and rings on through its release after you let go.");
      synthKnob(kHybridCutoff, "CUTOFF", h2[0], "The lowpass's cutoff at a middling breath (it follows the note half way).");
      synthKnob(kHybridResonance, "RESO", h2[1], "The lowpass's peak at its cutoff.");
      synthKnob(kHybridFilterBreath, "OPEN", h2[2], "How far the breath opens the lowpass: soft is dark, hard is bright.");
      synthKnob(kHybridFilterEnv, "F.ENV", h2[3], "How far the envelope moves the lowpass, up to 5 octaves up (or down, below 0).");
      synthKnob(kHybridAttack, "ATTACK", h3[0], "The oscillator's envelope: how long it takes to rise, 1 ms to 10 s.");
      synthKnob(kHybridDecay, "DECAY", h3[1], "How long it takes to fall from the top to the sustain.");
      synthKnob(kHybridSustain, "SUSTAIN", h3[2], "Where it holds while the note is held.");
      synthKnob(kHybridRelease, "RELEASE", h3[3], "How long it rings on after the note ends, up to 10 s (at Follow 0).");
      tabs.addToPage(1, add<ui::Label>({236, 370, 646, 16},
                                       "BLOWN BY THE SAME BREATH, OR A PAD ON ITS OWN ENVELOPE (FOLLOW 0)  ·  UNISON AND VIBRATO SHAPE IT TOO",
                                       kFaint, visage::Font::kLeft, 0.68f));

      // ALIEN: the throat its bowed string sounds through.
      throatRing_ = &tabs.addToPage(2, add<ThroatRing>({32, 104, 190, 276}, 190.0f));
      tips_.add(*throatRing_, "The Alien's throat travels round this ring of shapes: five vowels, and a ghost, an abyss and an "
                              "insect no throat has. The hollow ring is where each note starts; each dot is a voice's throat now.");
      formantGraph_ = &tabs.addToPage(2, add<FormantGraph>({236, 104, 250, 262}, 250.0f));
      tips_.add(*formantGraph_, "The throat's five resonances as they are now, and the note's harmonics they lift.");
      const auto a1 = ui::row({496, 90, 386, 112}, 6), a2 = ui::row({496, 206, 386, 112}, 6);
      const auto alienKnob = [&](clap_id id, const char *name, ui::Rect cell, const char *tip) {
         ui::Knob &k = knob(id, name, modelColor(kAlien), cell, tip);
         k.setTextSizes(8.5f, 9.5f, 8.5f);
         alienKnobs_.push_back(&k);
         tabs.addToPage(2, k);
      };
      alienKnob(kAlienVoice, "VOICE", a1[0], "Where on the ring each note's throat starts.");
      alienKnob(kAlienMorph, "MORPH", a1[1], "How fast the throat travels round the ring while a note is held: from still to a "
                                             "shape every third of a second.");
      alienKnob(kAlienDrift, "DRIFT", a1[2], "How far the throat wanders on its own, each resonance a little apart from the "
                                             "others: every note different.");
      alienKnob(kAlienTalk, "TALK", a2[0], "How much it talks: each note a new syllable, a consonant opening onto its own "
                                           "vowel, one after another like a sentence; the breath's pressure moves the mouth; "
                                           "turned up, it chatters on its own while a note is held.");
      alienKnob(kAlienShift, "SHIFT", a1[3], "Moves every resonance up or down an octave: a smaller or a larger throat.");
      alienKnob(kAlienSharp, "SHARP", a1[4], "How narrow the resonances are: wide and breathy, to ringing, to whistling.");
      alienKnob(kAlienLinger, "LINGER", a2[3], "The chance a vowel is held on for two or three times as long, more the "
                                               "harder it's blown.");
      alienKnob(kAlienGlide, "GLIDE", a2[2], "How quickly each syllable moves on to its vowel: a snap, as in speech, to a "
                                             "slow slide.");
      alienKnob(kAlienStrength, "CONSONANT", a2[1], "How strong each syllable's consonant is: none leaves only the vowels, all "
                                                     "of it the full stop, hiss or hum.");
      alienKnob(kAlienBend, "BEND", a2[4], "How far the throat follows a pitch bend (MPE or the wheel): none keeps "
                                           "the vowel where it is, all slides every resonance with the pitch.");
      alienKnob(kAlienBow, "BOW", a1[5], "How much a bowed string sings along with the voice: none is pure voice, all "
                                         "of it the string's buzz through the throat.");
      // The consonant families, each on or off.
      tabs.addToPage(2, add<ui::Label>({496, 330, 386, 14}, "CONSONANTS", kDim, visage::Font::kLeft, 0.68f));
      {
         static const char *const kFamilies[8] = {"M N", "L R", "W Y", "V Z", "B D G", "H", "F S", "P T K"};
         static const char *const kFamilyTips[8] = {
            "Nasals: m, n and ng, hummed through the nose.", "Liquids: l and r.", "Glides: w and y, sliding into the vowel.",
            "Voiced fricatives: v, th (as in this), z and zh, buzzing.", "Voiced stops: b, d, g and j.",
            "H: breath through the vowel's own shape.", "Fricatives: f, th, s and sh, a hiss at the lips or teeth.",
            "Stops: p, t, k and ch, a closure, a pop and a breath."};
         const auto fam = ui::row({496, 348, 386, 24}, 8, 4);
         for (int f = 0; f < 8; ++f) {
            auto &b = tabs.addToPage(2, add<ui::Button>(fam[f], kFamilies[f]));
            b.setAccent(modelColor(kAlien));
            b.setToggle(true);
            bindings_.bind(b, kAlienSounds + f);
            tips_.add(b, std::string(kFamilyTips[f]) + " Off, the Alien never says them.");
         }
      }
      alienKnob(kAlienPace, "PACE", a2[5], "How often a held note goes on to its next syllable. Free: by Talk. A note "
                                           "value: in time with the host's tempo.");
      tabs.addToPage(2, add<ui::Label>({236, 370, 460, 16},
                                       "THE ALIEN ONLY  ·  EVERY NOTE A SYLLABLE",
                                       kFaint, visage::Font::kLeft, 0.68f));
      tabs.select(0);
   }

   void buildBore() {
      shape_ = &add<BoreShape>({112, 414, 290, 26}, 290.0f);
      shape_->setModel(modelOf(host_));
      tips_.add(*shape_, "The bore's shape, which sets which harmonics it resonates on.");
      add<ui::Label>({404, 418, 52, 18}, "HOLES", kDim, visage::Font::kRight, 0.75f);
      tips_.add(choiceBox(kHoles, {462, 414, 96, 26}, 0xfff28c28),
                "How many tone holes below the note are open: more is brighter. The tube below rings most with them Closed.");
      add<ui::Label>({560, 418, 46, 18}, "TUBE", kDim, visage::Font::kRight, 0.75f);
      tips_.add(choiceBox(kTube, {612, 414, 96, 26}, 0xfff28c28), "What the instrument is made of: its body's colour.");
      add<ui::Label>({714, 418, 50, 18}, "MUTE", kDim, visage::Font::kRight, 0.75f);
      tips_.add(choiceBox(kMute, {770, 414, 110, 26}, 0xfff28c28),
                "A mute in the bell (or over the end of any instrument): straight, cup, harmon, plunger or bucket. "
                "MUTE AMT sets how far it's in.");
      bore_ = &add<BoreGraph>({32, 452, 848, 158}, 848.0f);
      tips_.add(*bore_, "The bore's resonances (its input impedance), with the loudest note's partials as it sounds.");
      const auto cells = ui::row({32, 618, 848, 100}, 11);
      const auto small = [&](clap_id id, const char *name, int cell, const char *tip) -> ui::Knob & {
         ui::Knob &k = familyKnob(id, name, cells[cell], tip);
         k.setTextSizes(9.0f, 10.5f, 9.0f);
         return k;
      };
      small(kFine, "TUNE", 0, "Fine tuning in cents.");
      small(kLength, "LENGTH", 1, "The tube below the open holes: its length sets where its own resonances ring.");
      small(kLoss, "LOSS", 2, "How much the bore loses each trip: more is softer, darker and slower to speak.");
      small(kHoleCutoff, "HOLE CUTOFF", 3, "Above this the tone holes let the sound out: lower is darker. The pitch stays put.");
      coneKnob_ = &small(kCone, "CONE", 4, "Sax and oboe: how far the cone is cut short at the reed, towards a cylinder or a full cone. Trumpet: how conical the bore is, towards a flugelhorn. Alien: how stiff its string is, which stretches its partials apart, from gut to wire to bar.");
      small(kRadiate, "RADIATE", 5, "How well the bell and holes send the highs out.");
      brassKnob_ = &small(kBrass, "BRASS", 6, "Trumpet: how far the wave steepens on its way down the horn, into a blare.");
      small(kBarrel, "BARREL", 7, "The barrel's or mouthpiece's cavity: bigger is lower and rounder.");
      ui::Knob &wall = small(kWall, "WALL", 8, "The walls: metal hardly soaks up the highs, soft wood and bone more.");
      wall.valueText = [](double v) { return std::string(wallName(v)); };
      small(kDetune, "DETUNE", 9, "Each note a little off, by up to this many cents: a section rather than one player.");
      small(kMuteAmount, "MUTE AMT", 10, "How far the mute is in, from open to all the way. Route slide or pressure to it to work a plunger.");
   }

   void buildPlayer() {
      playerTabs_ = &add<ui::TabBar>({28, 736, 440, 28}, std::vector<std::string>{"PLAYER", "VIBRATO", "LFOS", "BUSES"});
      playerTabs_->setAccent(0xfff28c28);
      auto &tabs = *playerTabs_;
      // PLAYER: the tongue, and how human the player is.
      tabs.addToPage(0, *(tongue_ = &add<TongueGraph>({32, 770, 330, 148}, 330.0f)));
      tabs.addToPage(0, add<ui::Label>({378, 776, 130, 16}, "STYLE", kDim, visage::Font::kLeft, 0.72f));
      tips_.add(tabs.addToPage(0, choiceBox(kTongueType, {378, 796, 130, 26}, 0xfff28c28)),
                "Soft: a gentle \"da\". Hard: a crisp \"ta\". Double: every note tongued, even slurred. "
                "Flutter: a rolled R through the note. Breath: no tongue, the air just starts.");
      const auto cells = ui::row({516, 784, 372, 120}, 5);
      const auto player = [&](clap_id id, const char *name, int cell, const char *tip) {
         ui::Knob &k = familyKnob(id, name, cells[cell], tip);
         k.setTextSizes(9.0f, 10.5f, 9.0f);
         tabs.addToPage(0, k);
      };
      player(kAttack, "ATTACK", 0, "How fast the breath rises (slower for a soft touch, quicker for a hard one).");
      player(kRelease, "RELEASE", 1, "How fast the breath dies away after the note.");
      player(kHumanize, "HUMANIZE", 2, "No two notes the same: each a little off in pitch, breath, attack and vibrato.");
      player(kFlutter, "FLUTTER", 3, "A flutter tongue, a rolled R through the note, on any style (the Flutter style rolls at least half).");
      player(kFlutterRate, "RATE", 4, "How fast the tongue flutters.");

      // VIBRATO: where it comes from, and how it moves.
      tabs.addToPage(1, *(vibrato_ = &add<VibratoGraph>({32, 770, 330, 148}, 330.0f)));
      tabs.addToPage(1, add<ui::Label>({378, 776, 130, 16}, "FROM", kDim, visage::Font::kLeft, 0.72f));
      tips_.add(tabs.addToPage(1, choiceBox(kVibType, {378, 796, 130, 26}, 0xfff28c28)),
                "Breath: from the diaphragm, in the air. Jaw: in the embouchure, so pitch and colour. Both. "
                "Lip: in the pitch alone.");
      const auto vib = ui::row({520, 784, 364, 120}, 4);
      tabs.addToPage(1, familyKnob(kVibRate, "RATE", vib[0], "Vibrato rate. A player's wanders a little."));
      tabs.addToPage(1, familyKnob(kVibDepth, "DEPTH", vib[1], "How deep the vibrato is."));
      tabs.addToPage(1, familyKnob(kVibPitch, "PITCH", vib[2], "Vibrato in the pitch, in cents, on top of its type."));
      tabs.addToPage(1, familyKnob(kVibDelay, "DELAY", vib[3], "How long a note is held before the vibrato grows in."));

      // LFOS: two LFOs and the Swell, each a source to arm with its chip.
      const char *const lfoTips[2] = {"LFO 1: a plain LFO, a source for routes. Note restarts it with each note; Free runs it for every voice together.",
                                      "LFO 2: a plain LFO, a source for routes. Note restarts it with each note; Free runs it for every voice together."};
      for (int i = 0; i < 3; ++i) {
         const float x = 32 + i * 284.0f;
         const int source = i < 2 ? kSrcLfo1 + i : kSrcSwell;
         tabs.addToPage(2, chip(source, {x, 772, 92, 24}));
         tabs.addToPage(2, choiceBox(i < 2 ? lfoParam(i, 1) : kSwellShape, {x + 98, 772, 96, 24}, sourceColor(source)));
         auto &sync = tabs.addToPage(2, add<ui::SegmentedSwitch>({x + 198, 772, 72, 24},
                                                                  i < 2 ? std::vector<std::string>{"NOTE", "FREE"}
                                                                        : std::vector<std::string>{"ONCE", "LOOP"}));
         sync.setAccent(sourceColor(source));
         bindings_.bind(sync, i < 2 ? lfoParam(i, 2) : kSwellLoop);
         generators_[i] = &tabs.addToPage(2, add<GeneratorGraph>({x, 804, 150, 114}, 150.0f, source));
         tabs.addToPage(2, knob(i < 2 ? lfoParam(i, 0) : kSwellTime, i < 2 ? "RATE" : "TIME", sourceColor(source),
                                {x + 154, 800, 116, 120},
                                i < 2 ? lfoTips[i] : "The Swell: one contour over this time from each note's start, a source for routes."));
      }

      // BUSES: mix routes into one, bend it, and send it on as a source of its own.
      for (int b = 0; b < kBusCount; ++b) {
         const float x = 32 + b * 213.0f;
         tabs.addToPage(3, chip(kSrcBus1 + b, {x, 772, 98, 24}));
         const unsigned bc = sourceColor(kSrcBus1 + b);
         tabs.addToPage(3, choiceBox(busParam(b, kBusMode), {x + 104, 772, 96, 24}, bc));
         tabs.addToPage(3, choiceBox(busParam(b, kBusCurve), {x, 802, 98, 24}, bc));
         tabs.addToPage(3, choiceBox(busParam(b, kBusPolarity), {x + 104, 802, 96, 24}, bc));
         tabs.addToPage(3, knob(busParam(b, kBusAmount), "AMOUNT", sourceColor(kSrcBus1 + b), {x, 830, 100, 96},
                                "The bus's base, which its routes are added to (or mixed with, by its mode). Route sources onto it in MAP."));
         tabs.addToPage(3, knob(busParam(b, kBusLag), "LAG", sourceColor(kSrcBus1 + b), {x + 100, 830, 100, 96},
                                "How slowly the bus follows its mix."));
      }
      tabs.select(0);
   }

   // A dropdown of a choice param's names, bound to it.
   ui::Dropdown &choiceBox(clap_id id, ui::Rect r, unsigned accent) {
      int count = 0;
      const char *const *names = choiceNames(id, count);
      auto &d = add<ui::Dropdown>(r, std::vector<std::string>(names, names + count));
      d.setAccent(accent);
      bindings_.bind(d, id);
      return d;
   }

   SourceChip &chip(int source, ui::Rect r) {
      auto &c = add<SourceChip>(r, source, upper(kModSourceNames[source]));
      c.onClick = [this](int src) { arm(src); };
      chips_.push_back(&c);
      tips_.add(c, std::string(kModSourceNames[source]) + ": touch to map it, then drag a knob to set how much it moves it.");
      return c;
   }

   // MAP and the source chips: the player's hands in the header, the breath's under the loops.
   void buildMap() {
      mapButton_ = &add<ui::Button>({772, 13, 64, 26}, "MAP");
      mapButton_->setToggle(true);
      mapButton_->setAccent(kPress);
      mapButton_->onClick = [this] { setMapping(!mapping_, kSrcOff); };
      tips_.add(*mapButton_, "MAP: touch a source, then drag any knob to set how much it moves it. Right-click a knob to unmap it.");
      const int hands[] = {kSrcStrike, kSrcPress, kSrcGlide, kSrcSlide, kSrcLift};
      for (int i = 0; i < 5; ++i)
         chip(hands[i], {846 + i * 106.0f, 13, 100, 26});
      const int breath[] = {kSrcLoop, kSrcKnot, kSrcFold, kSrcRegister, kSrcVapor, kSrcDraft};
      const float widths[] = {84, 84, 84, 112, 92, 92};
      float x = 912;
      for (int i = 0; i < 6; ++i) {
         breathChips_[i] = {&chip(breath[i], {x, 568, widths[i], 26}), {x, 568, widths[i], 26}};
         x += widths[i] + 6;
      }
      view_ = &add<ui::SegmentedSwitch>({1484, 568, 148, 26}, std::vector<std::string>{"LOOPS", "ROUTES"});
      view_->setAccent(kPress);
      view_->onChange = [this](int v) { showRoutes(v == 1); };
   }

   void buildRoutes() {
      routesTitle_ = &add<ui::Label>({936, 80, 300, 24}, "ROUTES", kPress, visage::Font::kLeft, 1.0f);
      routesCount_ = &add<ui::Label>({1200, 82, 180, 20}, "", kDim, visage::Font::kRight, 0.8f);
      auto &addRoute = add<ui::Button>({1392, 78, 90, 26}, "+ ADD");
      addRoute.onClick = [this] { addRouteNow(); };
      auto &prev = add<ui::Button>({1490, 78, 40, 26}, "<");
      prev.onClick = [this] { page(-1); };
      auto &next = add<ui::Button>({1536, 78, 40, 26}, ">");
      next.onClick = [this] { page(1); };
      routeChrome_ = {routesTitle_, routesCount_, &addRoute, &prev, &next};
      std::vector<std::string> sources(kModSourceNames, kModSourceNames + kModSourceCount), dests;
      for (int d = 0; d < kModDestCount; ++d)
         dests.push_back(kParams[kModDestinations[d]].name);
      for (int r = 0; r < kRouteCount; ++r) {
         RouteRow &row = rows_[r];
         const float y = 118 + (r % kRowsPerPage) * 54.0f;
         row.number = &add<ui::Label>({928, y + 2, 30, 24}, std::to_string(r + 1), kDim, visage::Font::kCenter, 0.9f);
         row.source = &add<ui::Dropdown>({966, y, 200, 26}, sources);
         bindings_.bind(*row.source, routeParam(r, kRouteSource));
         row.dest = &add<ui::Dropdown>({1174, y, 330, 26}, dests);
         bindings_.bind(*row.dest, routeParam(r, kRouteDestination));
         row.remove = &add<ui::Button>({1512, y, 34, 26}, "x");
         row.remove->onClick = [this, r] { removeRoute(r); };
         row.amount = &add<RouteAmount>({966, y + 30, 580, 18}, 580.0f, host_, r);
      }
      showRoutes(false);
   }

   void showRoutes(bool on) {
      routes_ = on;
      loops_->setVisible(!on);
      for (visage::Frame *f : routeChrome_)
         f->setVisible(on);
      for (int r = 0; r < kRouteCount; ++r) {
         const bool shown = on && r / kRowsPerPage == page_;
         RouteRow &row = rows_[r];
         for (visage::Frame *f : {static_cast<visage::Frame *>(row.number), static_cast<visage::Frame *>(row.source),
                                  static_cast<visage::Frame *>(row.dest), static_cast<visage::Frame *>(row.remove),
                                  static_cast<visage::Frame *>(row.amount)})
            f->setVisible(shown);
      }
      redraw();
   }
   void page(int by) {
      page_ = std::clamp(page_ + by, 0, kRouteCount / kRowsPerPage - 1);
      showRoutes(true);
   }
   int usedRoutes() const {
      int n = 0;
      for (int r = 0; r < kRouteCount; ++r)
         n += static_cast<int>(host_.paramValue(routeParam(r, kRouteSource)) + 0.5) != kSrcOff;
      return n;
   }
   void setRouteParam(int r, RouteParam p, double v) {
      host_.beginEdit(routeParam(r, p));
      host_.setParam(routeParam(r, p), v);
      host_.endEdit(routeParam(r, p));
   }
   void addRouteNow() {
      for (int r = 0; r < kRouteCount; ++r)
         if (static_cast<int>(host_.paramValue(routeParam(r, kRouteSource)) + 0.5) == kSrcOff) {
            setRouteParam(r, kRouteSource, armed_ != kSrcOff ? armed_ : kSrcPress);
            setRouteParam(r, kRouteDestination, 0);
            setRouteParam(r, kRouteAmount, 0.5);
            page_ = r / kRowsPerPage;
            showRoutes(true);
            return;
         }
   }
   void removeRoute(int r) {
      setRouteParam(r, kRouteSource, kSrcOff);
      setRouteParam(r, kRouteDestination, 0);
      setRouteParam(r, kRouteAmount, 0.0);
   }

   // ---- Mapper
   bool mapping() const override { return mapping_; }
   int armedSource() const override { return armed_; }
   void rings(int dest, std::vector<Ring> &out) const override {
      for (int r = 0; r < kRouteCount; ++r) {
         const int src = static_cast<int>(host_.paramValue(routeParam(r, kRouteSource)) + 0.5);
         const int dst = static_cast<int>(host_.paramValue(routeParam(r, kRouteDestination)) + 0.5);
         if (src != kSrcOff && dst == dest)
            out.push_back({r, src, host_.paramValue(routeParam(r, kRouteAmount))});
      }
   }
   int routeFor(int dest) override {
      int free = -1;
      for (int r = 0; r < kRouteCount; ++r) {
         const int src = static_cast<int>(host_.paramValue(routeParam(r, kRouteSource)) + 0.5);
         const int dst = static_cast<int>(host_.paramValue(routeParam(r, kRouteDestination)) + 0.5);
         if (src == armed_ && dst == dest)
            return r;
         if (src == kSrcOff && free < 0)
            free = r;
      }
      if (free >= 0) {
         setRouteParam(free, kRouteAmount, 0.0);
         setRouteParam(free, kRouteDestination, dest);
         setRouteParam(free, kRouteSource, armed_);
      }
      return free;
   }
   double routeAmount(int route) const override { return host_.paramValue(routeParam(route, kRouteAmount)); }
   void beginRoute(int route) override { host_.beginEdit(routeParam(route, kRouteAmount)); }
   void setRouteAmount(int route, double amount) override { host_.setParam(routeParam(route, kRouteAmount), amount); }
   void endRoute(int route) override {
      host_.endEdit(routeParam(route, kRouteAmount));
      if (std::fabs(routeAmount(route)) < 0.005)
         removeRoute(route);
   }
   void unmap(int dest) override {
      std::vector<Ring> found;
      rings(dest, found);
      for (const Ring &r : found)
         if (armed_ == kSrcOff || r.source == armed_)
            removeRoute(r.route);
   }

   void setMapping(bool on, int source) {
      mapping_ = on;
      armed_ = on ? source : kSrcOff;
      mapButton_->setOn(on);
      for (MapKnob *k : mapKnobs_)
         k->redraw();
   }
   // A chip touched: MAP on with it armed, or (touched again) disarmed.
   void arm(int source) {
      if (mapping_ && armed_ == source)
         setMapping(true, kSrcOff);
      else
         setMapping(true, source);
   }

   // Each tick: the chips' levels and MAP state, the live modulation on the knobs, the generator
   // graphs and the routes list.
   // The loudest sounding voice, whose sources and offsets the panel shows.
   const EditorHost::Voice *newestVoice() const {
      const EditorHost::Voice *newest = nullptr;
      for (const EditorHost::Voice &v : voices_)
         if (v.note >= 0 && !v.releasing && (!newest || v.level > newest->level))
            newest = &v;
      return newest;
   }
   void updateChips() {
      pulse_ += 0.08f;
      const float pulse = 0.5f + 0.5f * std::sin(pulse_);
      const EditorHost::Voice *newest = newestVoice();
      for (SourceChip *c : chips_) {
         if (!c->isVisible())
            continue;
         c->setState(mapping_, mapping_ && c->source() == armed_, pulse);
         const float v = newest ? newest->sources[c->source()] : 0.0f;
         const bool bi = bipolarSource(c->source());
         c->setLevel(bi ? 0.5f + 0.5f * v : v, bi);
      }
   }
   void updateMap() {
      updateChips();
      const EditorHost::Voice *newest = newestVoice();
      for (MapKnob *k : mapKnobs_) {
         if (!k->isVisible())
            continue;
         const int d = k->destination();
         const auto &p = kParams[kModDestinations[d]];
         const double off = newest ? newest->offsets[d] * (p.max - p.min) : 0.0;
         k->setLiveModulation(off, 0xffffffff);
      }
      generators_[0]->set(static_cast<int>(host_.paramValue(kLfo1Shape) + 0.5), newest ? newest->sources[kSrcLfo1] : 0.0);
      generators_[1]->set(static_cast<int>(host_.paramValue(kLfo2Shape) + 0.5), newest ? newest->sources[kSrcLfo2] : 0.0);
      generators_[2]->set(static_cast<int>(host_.paramValue(kSwellShape) + 0.5), newest ? newest->sources[kSrcSwell] : 0.0);
      loops_->hint = !mapping_ ? std::string()
                     : armed_ != kSrcOff ? "Now drag any knob to set how much " + upper(kModSourceNames[armed_]) + " moves it"
                                         : "MAP: touch a source, then drag a knob";
      loops_->hintColor = armed_ != kSrcOff ? sourceColor(armed_) : kPress;
      if (routes_) {
         char t[32];
         std::snprintf(t, sizeof(t), "%d of %d routes", usedRoutes(), kRouteCount);
         routesCount_->setText(t);
         for (int r = page_ * kRowsPerPage; r < (page_ + 1) * kRowsPerPage; ++r)
            rows_[r].amount->redraw();
      }
   }

   void buildVoice() {
      voiceTabs_ = &add<ui::TabBar>({912, 616, 440, 28}, std::vector<std::string>{"VOICE", "AIR", "REVERB", "DELAY"});
      voiceTabs_->setAccent(0xfff28c28);
      auto &tabs = *voiceTabs_;
      auto &mode = add<ui::SegmentedSwitch>({1368, 618, 252, 26}, std::vector<std::string>{"POLY", "MONO", "LEGATO"});
      mode.setAccent(0xfff28c28);
      bindings_.bind(mode, kVoiceMode);
      tips_.add(mode, "Poly: eight voices. Mono: one, tongued between notes. Legato: one, slurred between notes.");

      // VOICE: the voices, the family and the meters.
      lights_ = &tabs.addToPage(0, add<VoiceLights>({920, 668, 260, 100}, 260.0f));
      badge_ = &tabs.addToPage(0, add<FamilyBadge>({1220, 664, 180, 90}, 180.0f));
      const char *const meterNames[3] = {"IN", "OUT L", "OUT R"};
      for (int i = 0; i < 3; ++i) {
         auto &m = tabs.addToPage(0, add<ui::Meter>({1450 + i * 60.0f, 660, 16, 104}));
         (i == 0 ? inMeter_ : outMeters_[i - 1]) = &m;
         if (i > 0)
            m.setRange(-48.0f, 6.0f);
         tabs.addToPage(0, add<ui::Label>({1430 + i * 60.0f, 768, 56, 16}, meterNames[i], kDim, visage::Font::kCenter, 0.66f));
      }
      inMeter_->setRange(-36.0f, 0.0f);
      tips_.add(*inMeter_, "The breath coming in: the hardest-blown voice, or Breath In.");
      tips_.add(*outMeters_[0], "The output level, left and right.");

      // AIR: where the breath comes from and how it's shaped.
      curve_ = &tabs.addToPage(1, add<CurveGraph>({920, 652, 200, 136}, 200.0f));
      tips_.add(*curve_, "How pressure becomes breath. Each dot is a voice's pressure.");
      tabs.addToPage(1, add<ui::Label>({1132, 660, 100, 16}, "BREATH FROM", kDim, visage::Font::kLeft, 0.68f));
      auto &source = tabs.addToPage(1, add<ui::SegmentedSwitch>({1232, 656, 388, 26},
                                                                 std::vector<std::string>{"AUTO", "PRESS", "VEL", "INPUT", "MIX"}));
      source.setAccent(kPress);
      bindings_.bind(source, kBreathSource);
      tips_.add(source, "Where the breath comes from. Auto: velocity, until pressure, a breath controller (CC 2) or Breath In takes over. Input: only Breath In. Mix: Breath In is the lungs and each note's pressure (or velocity) how much it lets through.");
      const auto air = ui::row({1132, 686, 488, 104}, 3);
      tabs.addToPage(1, knob(kBreathIn, "BREATH IN", kPress, air[0],
                             "A breath to blow from: modulate it from a CV, an LFO or automation. Heard with Breath From on Input (or Auto)."));
      tabs.addToPage(1, knob(kCurve, "CURVE", kPress, air[1], "The press curve: above 1 you have to press harder."));
      tabs.addToPage(1, knob(kSlide, "SLIDE", 0xff8fe8a8, air[2], "How far MPE slide (CC 74) moves the embouchure."));

      // REVERB: on or off, the room, the tone and the limiter.
      const unsigned rc = 0xff7fd3ff;
      const auto onOff = [&](int page, clap_id id, const char *name, unsigned accent, const char *tip) {
         tabs.addToPage(page, add<ui::Label>({920, 664, 96, 16}, name, kDim, visage::Font::kLeft, 0.68f));
         auto &sw = tabs.addToPage(page, add<ui::SegmentedSwitch>({920, 686, 96, 26}, std::vector<std::string>{"OFF", "ON"}));
         sw.setAccent(accent);
         bindings_.bind(sw, id);
         tips_.add(sw, tip);
      };
      onOff(2, kReverbOn, "REVERB", rc, "The reverb on or off. Turned off, what's in the room rings out.");
      const auto room = ui::row({1024, 660, 460, 124}, 5);
      tabs.addToPage(2, knob(kRoomSize, "SIZE", rc, room[0], "From a small room to a hall."));
      tabs.addToPage(2, knob(kRoomDecay, "DECAY", rc, room[1], "How long the room rings."));
      tabs.addToPage(2, knob(kRoomDamp, "DAMP", rc, room[2], "How fast the room's highs die away."));
      tabs.addToPage(2, knob(kRoomPreDelay, "PRE-DELAY", rc, room[3], "The gap before the room answers."));
      tabs.addToPage(2, knob(kBright, "TONE", kDim, room[4], "A tilt above 1.5 kHz on the output."));
      tabs.addToPage(2, add<ui::Label>({1494, 668, 126, 16}, "LIMITER", kDim, visage::Font::kLeft, 0.68f));
      auto &limiter = tabs.addToPage(2, add<ui::SegmentedSwitch>({1494, 690, 126, 26}, std::vector<std::string>{"OFF", "SOFT", "HARD"}));
      limiter.setAccent(0xfff28c28);
      bindings_.bind(limiter, kLimiter);
      tips_.add(limiter, "Off: nothing. Soft: overs saturate. Hard: a peak limiter holds the output under full scale.");

      // DELAY: on or off, synced or free, and its repeats.
      const unsigned dc = 0xff8fe8a8;
      onOff(3, kDelayOn, "DELAY", dc, "The delay on or off. Turned off, its repeats fade out.");
      tabs.addToPage(3, add<ui::Label>({920, 724, 96, 16}, "SYNC", kDim, visage::Font::kLeft, 0.68f));
      tips_.add(tabs.addToPage(3, choiceBox(kDelaySync, {920, 746, 96, 26}, dc)),
                "Free: the Time knob, in ms. A note value: the delay follows the host's tempo.");
      const auto echo = ui::row({1024, 660, 596, 124}, 5);
      tabs.addToPage(3, knob(kDelayMix, "MIX", dc, echo[0], "How loud the repeats are under the dry sound."));
      delayTimeKnob_ = &knob(kDelayTime, "TIME", dc, echo[1], "The time between repeats, 10 ms to 2 s, when Sync is Free.");
      tabs.addToPage(3, *delayTimeKnob_);
      delayAccent_ = dc;
      tabs.addToPage(3, knob(kDelayFeedback, "FEEDBACK", dc, echo[2], "How much of each repeat comes back: how many repeats, and how long they last."));
      tabs.addToPage(3, knob(kDelayTone, "TONE", dc, echo[3], "How bright the repeats stay: lower, each one is darker than the last."));
      tabs.addToPage(3, knob(kDelaySpread, "SPREAD", dc, echo[4], "Straight down each side, or bouncing between left and right (ping-pong)."));
      tabs.select(0);

      const auto cells = ui::row({920, 800, 704, 120}, 11);
      const auto card = [&](clap_id id, const char *name, unsigned accent, int cell, const char *tip) {
         knob(id, name, accent, cells[cell], tip).setTextSizes(9.0f, 10.5f, 9.0f);
      };
      card(kSpread, "SPREAD", 0xfff28c28, 0, "Low notes to the left, high to the right, as across a section (below zero, the other way).");
      card(kPan, "PAN", 0xfff28c28, 1, "Where the notes sit, before the spread: modulate it per note (Key, Slide, Random...).");
      card(kUnison, "UNISON", 0xfff28c28, 2, "Copies of every voice, detuned against each other, to fatten it up.");
      card(kUnisonDetune, "U.DETUNE", 0xfff28c28, 3, "How far the unison copies spread, in cents, from the lowest to the highest.");
      card(kGlide, "GLIDE", 0xfff28c28, 4, "How long a slur takes to reach the next note (a finger lands in a few ms).");
      card(kBendRange, "BEND", 0xffb98bff, 5, "MPE pitch bend range of the note channels, in semitones.");
      card(kTune, "TRANSP", 0xfff28c28, 6, "Transposes in semitones.");
      card(kRoom, "REVERB", rc, 7, "How much reverb: shape it, or turn it off, on the REVERB tab.");
      card(kAir, "AIR", kPress, 8, "The breath heard around the instrument, not through it.");
      card(kLevel, "LEVEL", 0xfff28c28, 9, "Each voice's level (modulate it per note).");
      card(kMaster, "MASTER", 0xfff28c28, 10, "The output level, after the room and limiter.");
   }

   // The AIR view's own controls: the look, and the vapour and loops knobs.
   void buildAir() {
      playAir_ = &add<ui::SegmentedSwitch>({1500, 13, 132, 26}, std::vector<std::string>{"PLAY", "AIR"});
      playAir_->setAccent(0xfff28c28);
      playAir_->onChange = [this](int v) { setAir(v == 1); };
      tips_.add(*playAir_, "PLAY: the whole instrument. AIR: the breath loops and their smoke filling the window.");
      look_ = &add<ui::SegmentedSwitch>({928, 76, 140, 24}, std::vector<std::string>{"SMOKE", "INK"});
      look_->setAccent(0xff9c98d8);
      bindings_.bind(*look_, kLook);
      tips_.add(*look_, "Smoke rises off the loops and glows; ink drops from them and sinks.");
      palette_ = &choiceBox(kPalette, {1076, 76, 110, 24}, 0xff9c98d8);
      tips_.add(*palette_, "The smoke's colours.");
      airPanel_ = &add<AirBackdrop>({32, 712, 872, 168}, 872.0f);
      airPanelButton_ = &add<ui::Button>({1286, 76, 96, 24}, "CONTROLS");
      airPanelButton_->setToggle(true);
      airPanelButton_->setAccent(0xff9c98d8);
      airPanelButton_->setOn(!host_.airPanelHidden);
      airPanelButton_->onToggle = [this](bool on) {
         host_.airPanelHidden = !on;
         showAirPanel();
      };
      tips_.add(*airPanelButton_, "Show or hide the VAPOR, LOOPS and PLACE controls, leaving the window to the loops.");
      // Where the loops sit and how big they are, as JSynth places its creatures.
      const clap_id place[3] = {kLoopX, kLoopY, kLoopSize};
      const char *const placeTips[3] = {"What moves the loops left to right: the note's pitch by default.",
                                        "What lifts the loops up the window: the breath by default.",
                                        "What sets each loop's size: the breath by default."};
      for (int i = 0; i < 3; ++i) {
         auto &d = choiceBox(place[i], {680, 748.0f + 34 * i, 120, 24}, 0xffb98bff);
         tips_.add(d, placeTips[i]);
         airFrames_.push_back(&d);
      }
      auto &minSize = knob(kLoopSizeMin, "MIN SIZE", 0xffb98bff, {808, 744, 88, 128},
                           "The smallest a loop gets, as a share of the largest: its size runs from here to full.");
      airKnobs_.push_back(&minSize);
      const auto cells = ui::row({40, 744, 552, 128}, 6);
      const clap_id ids[6] = {kTurbulence, kRise, kSwirl, kTail, kLine, kGlow};
      const char *const names[6] = {"TURBULENCE", "RISE", "SWIRL", "TAIL", "LINE", "GLOW"};
      const char *const tips[6] = {"How hard the air curls the smoke.", "How fast warm smoke rises (or ink sinks).",
                                   "How tightly the smoke curls into strands.", "How long smoke hangs before it thins away.",
                                   "How bold the loops' line is.", "How much the loops and fresh smoke glow."};
      for (int i = 0; i < 6; ++i) {
         auto &k = knob(ids[i], names[i], i < 4 ? 0xff7fd3ff : 0xffb98bff, cells[i], tips[i]);
         airKnobs_.push_back(&k);
      }
      setAir(false);
   }

   // The AIR view: the loops fill the window and the rest of the panel steps aside.
   void setAir(bool on) {
      air_ = on;
      playAir_->setSelected(on ? 1 : 0);
      for (visage::Frame *f : playFrames_)
         f->setVisible(!on);
      if (!on) {
         // Each card shows only its tab's page again.
         exciterTabs_->showSelected();
         playerTabs_->showSelected();
         voiceTabs_->showSelected();
      }
      airPanelButton_->setVisible(on);
      showAirPanel();
      const ui::Rect loops = on ? ui::Rect{16, 64, 1616, 864} : kLoopsRect;
      move(*loops_, loops);
      loops_->resize(loops.w, loops.h);
      loops_->full = on;
      move(*look_, {loops.x + 16, loops.y + 12, 140, 24});
      move(*palette_, {loops.x + 164, loops.y + 12, 110, 24});
      move(*airPanelButton_, {loops.x + 282, loops.y + 12, 96, 24});
      // The breath's chips and the LOOPS / ROUTES switch along the loops' bottom edge.
      const float chipsY = on ? 892.0f : 568.0f;
      for (auto &[c, r] : breathChips_)
         move(*c, {on ? r.x - 880 : r.x, chipsY, r.w, r.h});
      move(*view_, {1484, chipsY, 148, 26});
      if (on && routes_) {
         view_->setSelected(0);
         showRoutes(false);
      }
      view_->setVisible(!on);
      // The dice card belongs to the PLAY view; it's back with it if it was open.
      for (visage::Frame *f : dice_)
         f->setVisible(!on && host_.diceOpen);
      redraw();
   }

   // The AIR view's VAPOR / LOOPS / PLACE panel, unless it's tucked away.
   void showAirPanel() {
      const bool on = air_ && !host_.airPanelHidden;
      airPanelButton_->setOn(!host_.airPanelHidden);
      airPanel_->setVisible(on);
      for (ui::Knob *k : airKnobs_)
         k->setVisible(on);
      for (visage::Frame *f : airFrames_)
         f->setVisible(on);
      redraw();
   }

   // The dice card, over the breath loops' top right, clear of every knob it changes: RANDOMIZE by
   // COMPLEXITY, MUTATE by AMOUNT, UNDO and REDO, and a lock for each section both leave alone.
   void buildDice() {
      const ui::Rect b = kDiceBox;
      const auto keep = [this](visage::Frame &f) -> visage::Frame & {
         dice_.push_back(&f);
         return f;
      };
      constexpr unsigned kAccent = 0xfff28c28;
      keep(add<DiceCard>(b, b.w));
      auto &rnd = add<ui::Button>({b.x + 14, b.y + 38, 132, 26}, "RANDOMIZE");
      rnd.setAccent(kAccent);
      rnd.onClick = [this] { roll(false); };
      keep(rnd);
      tips_.add(rnd, "A new patch from scratch: an instrument, then its exciter, bore, tongue and vibrato in ranges "
                     "that sound for it. Level, master, the MIDI setup and tuning stay as they are, and so do "
                     "locked sections.");
      auto &mut = add<ui::Button>({b.x + 14, b.y + 72, 132, 26}, "MUTATE");
      mut.setAccent(kAccent);
      mut.onClick = [this] { roll(true); };
      keep(mut);
      tips_.add(mut, "Nudges every value by AMOUNT. What's off stays off and a choice only moves to a near one, so "
                     "the patch stays itself. Click again to keep going.");
      const auto amountBox = [&](float y, const char *name, double &value, const char *tip) {
         auto &box = add<ui::NumberBox>({b.x + 154, y, b.w - 168, 26}, ui::ValueRange{0.0, 1.0, 0.3, 0.05});
         box.setAccent(kAccent);
         box.setLabel(name);
         box.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100.0))) + "%"; };
         box.parseText = [](const std::string &t, double &v) {
            char *end = nullptr;
            const double n = std::strtod(t.c_str(), &end);
            if (end == t.c_str())
               return false;
            v = std::clamp(n / 100.0, 0.0, 1.0);
            return true;
         };
         box.setValue(value);
         box.onChange = [&value](double v) { value = v; };
         keep(box);
         tips_.add(box, tip);
      };
      amountBox(b.y + 38, "COMPLEXITY", host_.dice.complexity,
                "How much RANDOMIZE reaches for: at 0 a plain, real instrument; higher brings growl and flutter, "
                "mutes, the HYBRID layer and pads, unison, LFOs and routes, the delay, wider bores and the Alien.");
      amountBox(b.y + 72, "AMOUNT", host_.dice.amount,
                "How far MUTATE moves each value: 5% is a touch, 25% a cousin of the patch, 100% a distant one. "
                "Only a high amount changes the instrument or adds a route.");
      auto &undo = add<ui::Button>({b.x + b.w - 124, b.y + 8, 54, 22}, "UNDO");
      undo.onClick = [this] { stepHistory(false); };
      undoButton_ = &undo;
      keep(undo);
      tips_.add(undo, "Back to the patch before the last RANDOMIZE or MUTATE.");
      auto &redo = add<ui::Button>({b.x + b.w - 66, b.y + 8, 52, 22}, "REDO");
      redo.onClick = [this] { stepHistory(true); };
      redoButton_ = &redo;
      keep(redo);
      tips_.add(redo, "Forward again to the patch UNDO left.");
      static const char *const lockTips[kDiceSectionCount] = {
         "the instrument and its exciter (breath, embouchure, stiffness, bite, aperture, noise)",
         "the bore's knobs, the holes, the tube and the mute",
         "the tongue: style, kick, attack, release, humanize, growl and flutter",
         "the vibrato",
         "the HYBRID layer",
         "the LFOs, the Swell, the buses and the routes",
         "the reverb and its tone, the air and the delay",
         "voice mode, glide, unison, spread and pan"};
      const auto cells = ui::row({b.x + 14, b.y + 138, b.w - 28, 24}, 4, 6);
      const auto cells2 = ui::row({b.x + 14, b.y + 168, b.w - 28, 24}, 4, 6);
      for (int s = 0; s < kDiceSectionCount; ++s) {
         auto &lock = add<ui::Button>(s < 4 ? cells[s] : cells2[s - 4], kDiceSectionNames[s]);
         lock.setAccent(kAccent);
         lock.setToggle(true);
         lock.setOn(host_.dice.locked(s));
         lock.onToggle = [this, s](bool on) {
            host_.dice.locks = on ? host_.dice.locks | (1u << s) : host_.dice.locks & ~(1u << s);
         };
         keep(lock);
         tips_.add(lock, std::string("Locked, RANDOMIZE and MUTATE leave ") + lockTips[s] + " as they are.");
      }
   }
   // Every param, with the preset's name, as the dice found them.
   DiceSnapshot snapshot() const {
      DiceSnapshot now;
      now.values.resize(kParamCount);
      for (clap_id id = 0; id < kParamCount; ++id)
         now.values[id] = host_.paramValue(id);
      now.name = host_.presetName();
      return now;
   }
   // Puts a patch in, one edit for each param that changes, as loading a preset does. The look
   // stays as it is.
   void restore(const DiceSnapshot &s) {
      for (clap_id id = 0; id < kParamCount; ++id) {
         if (isLookParam(id) || host_.paramValue(id) == s.values[id])
            continue;
         host_.beginEdit(id);
         host_.setParam(id, s.values[id]);
         host_.endEdit(id);
      }
      host_.setPresetName(s.name);
      bindings_.sync();
      presetBar_->redraw();
      redraw();
   }
   // Randomize or mutate, keeping what it was to go back to.
   void roll(bool mutate) {
      DiceSnapshot now = snapshot();
      std::vector<double> values = now.values;
      // Each roll is new: the clock and a count, mixed by the dice's own generator.
      const uint64_t seed = DiceRng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                    ++diceSeed_).next();
      std::string name;
      if (mutate) {
         mutatePatch(values, host_.dice, seed);
         name = mutantName(now.name);
      } else
         name = randomizePatch(values, host_.dice, seed);
      host_.diceHistory.push(std::move(now));
      restore({values, name});
      refreshDice();
   }
   void stepHistory(bool forward) {
      DiceHistory &h = host_.diceHistory;
      if (forward ? !h.canRedo() : !h.canUndo())
         return;
      restore(forward ? h.redo(snapshot()) : h.undo(snapshot()));
      refreshDice();
   }
   // UNDO and REDO dim when there's nothing to step to.
   void refreshDice() {
      undoButton_->setAccent(host_.diceHistory.canUndo() ? 0xfff28c28 : kFaint);
      redoButton_->setAccent(host_.diceHistory.canRedo() ? 0xfff28c28 : kFaint);
      undoButton_->redraw();
      redoButton_->redraw();
   }

   static constexpr int kRowsPerPage = 8;
   struct RouteRow {
      ui::Label *number = nullptr;
      ui::Dropdown *source = nullptr, *dest = nullptr;
      ui::Button *remove = nullptr;
      RouteAmount *amount = nullptr;
   };

   static constexpr ui::Rect kLoopsRect{912, 64, 720, 494};
   static constexpr ui::Rect kDiceBox{1252, 104, 368, 206};

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::FramePacer pacer_{60.0f};
   float fpsTime_ = 0.0f, fps_ = 0.0f;
   int frameCount_ = 0;
   unsigned tick_ = 0;
   bool air_ = false;
   std::vector<visage::Frame *> playFrames_;
   std::pair<SourceChip *, ui::Rect> breathChips_[6] = {};
   ui::SegmentedSwitch *playAir_ = nullptr, *look_ = nullptr;
   ui::Dropdown *palette_ = nullptr;
   visage::Frame *airPanel_ = nullptr;
   ui::Button *airPanelButton_ = nullptr;
   std::vector<ui::Knob *> airKnobs_;
   std::vector<visage::Frame *> airFrames_;
   ui::TabBar *exciterTabs_ = nullptr, *voiceTabs_ = nullptr;
   FilterGraph *filterGraph_ = nullptr;
   ThroatRing *throatRing_ = nullptr;
   FormantGraph *formantGraph_ = nullptr;
   std::vector<ui::Knob *> alienKnobs_;
   ui::Knob *coneKnob_ = nullptr, *brassKnob_ = nullptr, *delayTimeKnob_ = nullptr;
   unsigned delayAccent_ = 0;
   ui::Meter *inMeter_ = nullptr, *outMeters_[2] = {};
   ui::TabBar *playerTabs_ = nullptr;
   ui::Button *mapButton_ = nullptr;
   ui::SegmentedSwitch *view_ = nullptr;
   std::vector<SourceChip *> chips_;
   std::vector<MapKnob *> mapKnobs_;
   GeneratorGraph *generators_[3] = {};
   RouteRow rows_[kRouteCount];
   ui::Label *routesTitle_ = nullptr, *routesCount_ = nullptr;
   std::vector<visage::Frame *> routeChrome_;
   bool mapping_ = false, routes_ = false;
   int armed_ = kSrcOff, page_ = 0;
   float pulse_ = 0.0f;
   InstrumentList *list_ = nullptr;
   ValveGraph *valve_ = nullptr;
   BoreGraph *bore_ = nullptr;
   BoreShape *shape_ = nullptr;
   TongueGraph *tongue_ = nullptr;
   VibratoGraph *vibrato_ = nullptr;
   CurveGraph *curve_ = nullptr;
   VoiceLights *lights_ = nullptr;
   BreathLoops *loops_ = nullptr;
   FamilyBadge *badge_ = nullptr;
   ui::Label *rangeText_ = nullptr;
   std::vector<ui::Knob *> familyKnobs_;
   ui::PresetBar *presetBar_ = nullptr;
   ui::Button *undoButton_ = nullptr, *redoButton_ = nullptr;
   std::vector<visage::Frame *> dice_;
   uint64_t diceSeed_ = 0;
   std::vector<EditorHost::Voice> voices_;
   int shownModel_ = -1;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, int frames, const char *pngPath,
                      const ScreenshotOptions &options) {
   gSyncSmoke = true;
   Panel panel(host);
   panel.open(options);
   for (int i = 0; i < frames; ++i)
      panel.frame(1.0f / 30.0f);
   return ui::renderToPng(panel, width, height, pngPath);
}

PanelBenchmark benchmarkPanel(EditorHost &host, int width, int height, int frames, bool air) {
   // The smoke solved on this thread, so its cost shows in the step (in the plugin it has its own).
   gSyncSmoke = true;
   Panel panel(host);
   ScreenshotOptions options;
   options.air = air;
   panel.open(options);
   visage::ApplicationEditor editor;
   editor.addChild(&panel);
   editor.setWindowless(width, height);
   panel.setBounds(editor.localBounds());
   using Clock = std::chrono::steady_clock;
   PanelBenchmark b;
   double step = 0.0, draw = 0.0;
   for (int i = 0; i < frames; ++i) {
      const auto t0 = Clock::now();
      panel.frame(1.0f / 60.0f);
      const auto t1 = Clock::now();
      // Drawing what changed, as a window does at each refresh (with the GPU's work waited for).
      editor.drawWindow();
      editor.takeScreenshot();
      const auto t2 = Clock::now();
      // The first frames build the smoke up; time the steady state.
      if (i >= frames / 2) {
         step += std::chrono::duration<double, std::milli>(t1 - t0).count();
         draw += std::chrono::duration<double, std::milli>(t2 - t1).count();
      }
   }
   const int timed = frames - frames / 2;
   b.stepMs = step / std::max(1, timed);
   b.drawMs = draw / std::max(1, timed);
   b.cells = panel.smokeCells();
   editor.removeChild(&panel);
   return b;
}

} // namespace jbreath
