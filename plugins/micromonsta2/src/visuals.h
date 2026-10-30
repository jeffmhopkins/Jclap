#pragma once

// The panel's pictures: small drawings made only from the knobs' CC values and the program's
// routing. The plugin can't hear the synth or see its LFOs, so none of them claim to show the sound;
// they are guides to what the numbers mean. Only editor.cpp includes this.

#include <jclap/ui/slider.h>
#include <jclap/ui/theme.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <visage/ui.h>

namespace mm2::visuals {

namespace ui = jclap::ui;

// A CC's current 0-127 value, by CC number.
using CcValue = std::function<double(int cc)>;

inline unsigned withAlpha(unsigned color, unsigned alpha) { return (color & 0x00ffffff) | (alpha << 24); }

// The panel's look: translucent widgets on dark cards, like JSynth's.
inline const ui::Theme &glassTheme() {
   static const ui::Theme theme = [] {
      ui::Theme t;
      t.background = 0xff090c13;
      t.track = 0x40ffffff;
      t.knobLight = 0xff3a4152;
      t.knobDark = 0xff141823;
      t.dimText = 0xff9aa3b5;
      t.button = 0x40141a26;
      t.buttonHover = 0x60283040;
      t.panel = 0x80141a26;
      t.panelBorder = 0x40ffffff;
      return t;
   }();
   return theme;
}

// Base for a picture: ignores the mouse and draws on a dark inset like the envelope editor.
class Picture : public visage::Frame {
public:
   explicit Picture(CcValue value) : value_(std::move(value)) { setIgnoresMouseEvents(true, false); }

   // Redraws when any value it reads has changed (called from the panel's timer).
   void refresh() {
      const double sum = signature();
      if (sum != shown_) {
         shown_ = sum;
         redraw();
      }
   }

protected:
   virtual double signature() const = 0;
   double cc(int n) const { return value_ ? std::clamp(value_(n), 0.0, 127.0) : 0.0; }
   // Design units: these pictures are drawn for their design height.
   float s(float designHeight) const { return height() / designHeight; }

   void inset(visage::Canvas &c) {
      const ui::Theme &t = glassTheme();
      const float r = std::min(width(), height()) * 0.05f;
      c.setColor(t.knobDark);
      c.roundedRectangle(0, 0, width(), height(), r);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), r, 1.0f);
   }
   // A polyline with a fill under it to `floor`, one pixel column at a time.
   static void fillUnder(visage::Canvas &c, const float *xs, const float *ys, int n, float floor) {
      for (int i = 0; i + 1 < n; ++i) {
         const float span = xs[i + 1] - xs[i];
         for (float x = xs[i]; x < xs[i + 1]; x += 1.0f) {
            const float y = span > 0.0f ? ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / span : ys[i];
            c.fill(x, y, std::min(1.0f, xs[i + 1] - x), std::max(0.0f, floor - y));
         }
      }
   }
   static void line(visage::Canvas &c, const float *xs, const float *ys, int n, float thick, int dash = 0) {
      for (int i = 0; i + 1 < n; ++i)
         if (!dash || (i / dash) % 2 == 0)
            c.segment(xs[i], ys[i], xs[i + 1], ys[i + 1], thick, true);
   }

private:
   CcValue value_;
   double shown_ = -1.0;
};

// A low-pass response from cutoff (CC 74) and resonance (CC 71), with a dashed second curve for where
// the filter envelope (CC 88, centred) takes the cutoff at its peak. The synth's filter type isn't a
// CC, so this is a guide, not a measurement.
class FilterCurve : public Picture {
public:
   using Picture::Picture;
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float k = s(176.0f), w = width(), h = height(), pad = 8 * k;
      inset(c);
      constexpr int kPoints = 160;
      const auto xAt = [&](int i) { return pad + (w - 2 * pad) * i / (kPoints - 1); };
      // 20 Hz to 20 kHz across, +18 dB to -48 dB down.
      const auto yOf = [&](float db) { return pad + (h - 2 * pad) * (18.0f - std::clamp(db, -48.0f, 18.0f)) / 66.0f; };
      for (float f : {100.0f, 1000.0f, 10000.0f}) {
         const float x = pad + (w - 2 * pad) * std::log(f / 20.0f) / std::log(1000.0f);
         c.setColor(t.track);
         c.fill(x, 0, 1, h);
         c.setColor(t.dimText);
         c.text(f < 1000 ? "100" : f < 10000 ? "1k" : "10k", ui::font(10 * k), visage::Font::kLeft, x + 4 * k,
                h - 18 * k, 40 * k, 14 * k);
      }
      c.setColor(t.track);
      c.fill(0, yOf(0), w, 1);

      const float cutoff = static_cast<float>(cc(74)), res = static_cast<float>(cc(71));
      const float peak = std::clamp(cutoff + (static_cast<float>(cc(88)) - 64.0f) * 0.9f, 0.0f, 127.0f);
      const float q = 0.6f + 6.0f * res / 127.0f;
      float xs[kPoints], ys[kPoints], ye[kPoints];
      const auto curve = [&](float cut, float *out) {
         const float fc = 20.0f * std::pow(1000.0f, cut / 127.0f);
         for (int i = 0; i < kPoints; ++i) {
            const float f = 20.0f * std::pow(1000.0f, i / float(kPoints - 1)), x = f / fc;
            const float m = 1.0f / std::sqrt((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
            out[i] = yOf(20.0f * std::log10(m * m));
         }
      };
      for (int i = 0; i < kPoints; ++i)
         xs[i] = xAt(i);
      curve(cutoff, ys);
      curve(peak, ye);
      c.setColor(withAlpha(t.accent, 0x30));
      fillUnder(c, xs, ys, kPoints, h - 1);
      c.setColor(withAlpha(0xff9fc3ee, 0xc0));
      line(c, xs, ye, kPoints, 1.4f * k, 3);
      c.setColor(t.accent);
      line(c, xs, ys, kPoints, 2.2f * k);
      // The handle sits on the curve's highest point near the cutoff.
      const float hx = pad + (w - 2 * pad) * cutoff / 127.0f;
      const float hy = *std::min_element(ys, ys + kPoints);
      c.setColor(withAlpha(t.accent, 0x40));
      c.circle(hx - 11 * k, hy - 11 * k, 22 * k);
      c.setColor(t.accent);
      c.circle(hx - 7 * k, hy - 7 * k, 14 * k);
   }

protected:
   double signature() const override { return cc(74) + 128 * cc(71) + 16384 * cc(88); }
};

// The four mix levels (CC 48-51) side by side, as wide as they are loud.
class MixBar : public Picture {
public:
   MixBar(CcValue value, const unsigned (&colors)[4]) : Picture(std::move(value)) {
      std::copy(colors, colors + 4, colors_);
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float w = width(), h = height();
      double total = 0;
      for (int i = 0; i < 4; ++i)
         total += cc(48 + i);
      c.setColor(t.track);
      c.roundedRectangle(0, 0, w, h, 2);
      if (total <= 0)
         return;
      float x = 0;
      for (int i = 0; i < 4; ++i) {
         const float part = static_cast<float>(cc(48 + i) / total) * w;
         if (part >= 1.0f) {
            c.setColor(colors_[i]);
            c.fill(x, 0, std::max(1.0f, part - 2.0f), h);
         }
         x += part;
      }
   }

protected:
   double signature() const override { return cc(48) + 128 * cc(49) + 16384 * cc(50) + 2097152 * cc(51); }

private:
   unsigned colors_[4] = {};
};

// Delay echoes: spaced by TIME (CC 26), each one quieter by FEEDBACK (CC 27). Dimmed when LEVEL (CC 28)
// is 0, since nothing of it is heard then.
class DelayTaps : public Picture {
public:
   DelayTaps(CcValue value, unsigned color) : Picture(std::move(value)), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float k = s(72.0f), w = width(), h = height(), pad = 10 * k, floor = h - pad;
      inset(c);
      const bool heard = cc(28) > 0;
      const unsigned color = heard ? color_ : withAlpha(color_, 0x70);
      c.setColor(t.track);
      c.fill(pad, floor, w - 2 * pad, 1);
      const float spacing = (8 + 38 * static_cast<float>(cc(26)) / 127.0f) * k;
      const float fb = static_cast<float>(cc(27)) / 127.0f;
      float level = 1.0f;
      for (float x = pad + 2 * k; x < w - pad && level > 0.04f; x += spacing) {
         const float bar = (floor - pad - 10 * k) * level;
         c.setColor(color);
         c.fill(x, floor - bar, 3 * k, bar);
         level *= fb * 0.92f;
         if (fb <= 0.0f)
            break;
      }
      if (!heard) {
         c.setColor(t.dimText);
         c.text("LEVEL 0 · NOT HEARD", ui::font(9 * k), visage::Font::kRight, 0, 3 * k, w - 8 * k, 12 * k);
      }
   }

protected:
   double signature() const override { return cc(26) + 128 * cc(27) + 16384 * cc(28); }

private:
   unsigned color_;
};

// A reverb tail: as long as DECAY (CC 29), rippled by MOD (CC 30), dimmed when LEVEL (CC 31) is 0.
class ReverbTail : public Picture {
public:
   ReverbTail(CcValue value, unsigned color) : Picture(std::move(value)), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float k = s(72.0f), w = width(), h = height(), pad = 10 * k, floor = h - pad;
      inset(c);
      const bool heard = cc(31) > 0;
      const unsigned color = heard ? color_ : withAlpha(color_, 0x70);
      const float length = (0.12f + 0.88f * static_cast<float>(cc(29)) / 127.0f) * (w - 2 * pad);
      const float ripple = static_cast<float>(cc(30)) / 127.0f;
      constexpr int kPoints = 80;
      float xs[kPoints], ys[kPoints];
      for (int i = 0; i < kPoints; ++i) {
         const float f = i / float(kPoints - 1);
         const float env = std::exp(-5.0f * f) * (1.0f + 0.12f * ripple * std::sin(f * 40.0f));
         xs[i] = pad + f * length;
         ys[i] = floor - (floor - pad - 10 * k) * std::clamp(env, 0.0f, 1.0f);
      }
      c.setColor(withAlpha(color, heard ? 0x40 : 0x20));
      fillUnder(c, xs, ys, kPoints, floor);
      c.setColor(color);
      line(c, xs, ys, kPoints, 1.6f * k);
      c.setColor(t.track);
      c.fill(pad, floor, w - 2 * pad, 1);
      if (!heard) {
         c.setColor(t.dimText);
         c.text("LEVEL 0 · NOT HEARD", ui::font(9 * k), visage::Font::kRight, 0, 3 * k, w - 8 * k, 12 * k);
      }
   }

protected:
   double signature() const override { return cc(29) + 128 * cc(30) + 16384 * cc(31); }

private:
   unsigned color_;
};

// The EQ (CC 25) as a tilt: 64 is flat, lower leans to the lows, higher to the highs.
class EqTilt : public Picture {
public:
   EqTilt(CcValue value, unsigned color) : Picture(std::move(value)), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float k = s(72.0f), w = width(), h = height(), pad = 14 * k, mid = h * 0.5f;
      inset(c);
      c.setColor(t.track);
      c.fill(w * 0.5f, pad, 1, h - 2 * pad);
      const float tilt = (static_cast<float>(cc(25)) - 64.0f) / 64.0f * (h * 0.5f - pad);
      c.setColor(color_);
      c.segment(pad, mid + tilt, w - pad, mid - tilt, 2.0f * k, true);
      c.circle(w * 0.5f - 4 * k, mid - 4 * k, 8 * k);
      c.setColor(t.dimText);
      const auto f = ui::font(9 * k);
      c.text("LOW", f, visage::Font::kLeft, 8 * k, h - 15 * k, 40 * k, 12 * k);
      c.text("HIGH", f, visage::Font::kRight, w - 48 * k, h - 15 * k, 40 * k, 12 * k);
      const double v = cc(25);
      c.text(v > 66 ? "BRIGHTER" : v < 62 ? "DARKER" : "FLAT", f, visage::Font::kRight, 0, 3 * k, w - 8 * k, 12 * k);
   }

protected:
   double signature() const override { return cc(25); }

private:
   unsigned color_;
};

// Pan spread (CC 35) as a fan of voices across the stereo field.
class PanFan : public Picture {
public:
   using Picture::Picture;
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float k = s(100.0f), w = width(), h = height();
      inset(c);
      const float cx = w * 0.5f, cy = h - 18 * k, r = std::min(w * 0.5f - 16 * k, h - 34 * k);
      constexpr float kPi = 3.14159265f;
      c.setColor(t.track);
      c.arc(cx - r, cy - r, 2 * r, 1.5f * k, -0.5f * kPi, 0.5f * kPi, true);
      const float spread = static_cast<float>(cc(35)) / 127.0f;
      for (int i = 0; i < 5; ++i) {
         const float a = (i - 2) / 2.0f * spread * 0.45f * kPi;
         c.setColor(i == 2 ? t.accent : withAlpha(t.accent, 0xa0));
         c.segment(cx, cy, cx + std::sin(a) * r, cy - std::cos(a) * r, 2.0f * k, true);
         if (spread <= 0.0f)
            break;
      }
      c.setColor(t.dimText);
      const auto f = ui::font(9 * k);
      c.text("L", f, visage::Font::kLeft, 8 * k, h - 15 * k, 20 * k, 12 * k);
      c.text("R", f, visage::Font::kRight, w - 28 * k, h - 15 * k, 20 * k, 12 * k);
      c.text(spread > 0 ? "VOICES SPREAD" : "ALL VOICES CENTRED", f, visage::Font::kCenter, 0, 4 * k, w, 12 * k);
   }

protected:
   double signature() const override { return cc(35); }
};

// The arpeggiator's 16 steps as a little piano roll: one column per step, a block on the lane of the
// held note it plays (1st at the bottom), as wide as the gate. AUTO steps are outlined and follow
// the style; ties join the step before, chords stack, and steps past the length are dim. Reads the
// settings by CLAP id (idBase + NRPN), as the panel's host has them.
class ArpPattern : public Picture {
public:
   ArpPattern(CcValue value, unsigned accent, int idBase) : Picture(std::move(value)), accent_(accent), base_(idBase) {}

   void draw(visage::Canvas &c) override {
      inset(c);
      const ui::Theme &t = glassTheme();
      const float w = width(), h = height(), col = w / 16.0f;
      constexpr int kLanes = 6;
      const float top = h * 0.06f, laneH = (h * 0.88f) / kLanes;
      const auto laneY = [&](int lane) { return top + (kLanes - 1 - lane) * laneH; };
      c.setColor(withAlpha(t.panelBorder, 0x90));
      for (int l = 1; l < kLanes; ++l)
         c.fill(0, laneY(l) + laneH, w, 1.0f);
      for (int i = 1; i < 16; ++i)
         c.fill(i * col, 0, 1.0f, h);

      const bool on = setting(130) > 0;
      const int length = setting(133) + 1, style = setting(114);
      const float gate = 0.25f + 0.75f * static_cast<float>(setting(115)) / 127.0f;
      int lane = 0, autoIndex = 0;
      for (int i = 0; i < 16; ++i) {
         const int step = setting(134 + i);
         const unsigned color = withAlpha(accent_, (i < length ? 0xff : 0x40) * (on ? 4u : 2u) / 4u);
         const float x = i * col + col * 0.12f, bw = (col * 0.76f) * gate, pad = laneH * 0.18f;
         const auto block = [&](int l, bool outline) {
            c.setColor(color);
            if (outline)
               c.roundedRectangleBorder(x, laneY(l) + pad, bw, laneH - 2 * pad, 3.0f, 1.5f);
            else
               c.roundedRectangle(x, laneY(l) + pad, bw, laneH - 2 * pad, 3.0f);
         };
         switch (step) {
         case 0: // auto: the style's next note
            lane = autoLane(style, autoIndex++, kLanes);
            block(lane, true);
            break;
         case 1: // off: a rest
            c.setColor(color);
            c.fill(x, h - top - 3.0f, col * 0.76f, 2.0f);
            break;
         case 2: // tie: holds the last note into this step
            c.setColor(color);
            c.roundedRectangle(i * col - col * 0.2f, laneY(lane) + laneH * 0.36f, col * 1.1f, laneH * 0.28f, 2.0f);
            break;
         case 3: lane = std::max(0, lane - 1); block(lane, false); break;
         case 10: lane = std::min(kLanes - 1, lane + 1); block(lane, false); break;
         case 11: // chord: every held note
            for (int l = 0; l < 4; ++l)
               block(l, false);
            break;
         default: // 1st-6th held note
            lane = std::clamp(step - 4, 0, kLanes - 1);
            block(lane, false);
            break;
         }
      }
      c.setColor(t.dimText);
      const std::string octaves = std::to_string(setting(132) + 1) + (setting(132) > 0 ? " OCTAVES" : " OCTAVE");
      c.text(on ? octaves : "ARP OFF", ui::font(h * 0.035f), visage::Font::kRight, 0, h * 0.01f, w - h * 0.02f,
             h * 0.05f);
   }

protected:
   double signature() const override {
      double sum = 0;
      for (int n = 114; n <= 149; ++n)
         sum = sum * 1.000003 + cc(base_ + n) * (n % 13 + 1);
      return sum;
   }

private:
   int setting(int nrpn) const { return static_cast<int>(std::lround(cc(base_ + nrpn))); }
   // Which held note an AUTO step plays, for the styles: up, 3 up, down, 3 down, up/down, random,
   // as played. A guide with four notes held.
   static int autoLane(int style, int k, int lanes) {
      constexpr int kHeld = 4;
      switch (style) {
      case 2:
      case 3: return kHeld - 1 - k % kHeld;
      case 4: {
         const int p = k % (2 * kHeld - 2);
         return p < kHeld ? p : 2 * kHeld - 2 - p;
      }
      case 5: return static_cast<int>((k * 7 + 3) % kHeld);
      default: return std::min(lanes - 1, k % kHeld);
      }
   }

   unsigned accent_;
   int base_;
};

// One of the synth's own knobs (or an encoder, with ticks round it), drawn for the knob page.
class KnobGlyph : public visage::Frame {
public:
   explicit KnobGlyph(bool encoder) : encoder_(encoder) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float d = std::min(width(), height()), cx = width() * 0.5f, cy = height() * 0.5f;
      const float r = d * (encoder_ ? 0.36f : 0.42f);
      if (encoder_) {
         c.setColor(t.dimText);
         for (int i = 0; i < 24; ++i) {
            const float a = static_cast<float>(i) * 6.2831853f / 24.0f;
            c.segment(cx + std::cos(a) * r * 1.18f, cy + std::sin(a) * r * 1.18f, cx + std::cos(a) * r * 1.3f,
                      cy + std::sin(a) * r * 1.3f, 1.5f, true);
         }
      }
      c.setColor(visage::Brush::linear(visage::Color(t.knobLight), visage::Color(t.knobDark), visage::Point(cx, cy - r),
                                       visage::Point(cx, cy + r)));
      c.circle(cx - r, cy - r, 2 * r);
      c.setColor(t.knobEdge);
      c.ring(cx - r, cy - r, 2 * r, 1.5f);
      c.setColor(t.text);
      c.segment(cx, cy - r * 0.25f, cx, cy - r * 0.85f, 3.0f, true);
   }

private:
   bool encoder_;
};

// A card for a group of controls, with its title in the accent colour.
class Card : public visage::Frame {
public:
   Card(std::string title, unsigned accent, float designHeight)
       : title_(std::move(title)), accent_(accent), designHeight_(designHeight) {
      setIgnoresMouseEvents(true, false);
   }
   void draw(visage::Canvas &c) override {
      const float s = height() / designHeight_;
      c.setColor(visage::Color(0xc80b0e17));
      c.roundedRectangle(0, 0, width(), height(), 10 * s);
      c.setColor(visage::Color(0x2effffff));
      c.roundedRectangleBorder(0, 0, width(), height(), 10 * s, 1.0f * s);
      if (!title_.empty()) {
         c.setColor(accent_);
         c.text(title_, ui::font(14 * s), visage::Font::kLeft, 16 * s, 8 * s, width(), 20 * s);
      }
   }

private:
   std::string title_;
   unsigned accent_;
   float designHeight_;
};

// One cycle of an oscillator: a guide drawn from its algorithm (a setting, read by CLAP id) and its
// shape knob, since the synth's own waves aren't published. FM shows the shape as the FM depth;
// a wavetable shows a made-up blend of harmonics that moves with the shape.
class OscWave : public Picture {
public:
   OscWave(CcValue value, unsigned color, int algoId, int shapeCc, bool fmChoices)
       : Picture(std::move(value)), color_(color), algoId_(algoId), shapeCc_(shapeCc), fm_(fmChoices) {}

   void draw(visage::Canvas &c) override {
      inset(c);
      const float w = width(), h = height(), mid = h * 0.5f, amp = h * 0.36f;
      c.setColor(withAlpha(glassTheme().panelBorder, 0x50));
      c.fill(0, mid, w, 1.0f);
      const int algo = static_cast<int>(std::lround(cc(algoId_)));
      const double shape = cc(shapeCc_) / 127.0;
      constexpr int kN = 160;
      float xs[kN], ys[kN];
      for (int i = 0; i < kN; ++i) {
         const double p = static_cast<double>(i) / (kN - 1);
         xs[i] = 4.0f + (w - 8.0f) * static_cast<float>(p);
         ys[i] = mid - amp * static_cast<float>(std::clamp(sample(algo, shape, p), -1.0, 1.0));
      }
      c.setColor(withAlpha(color_, 0x30));
      for (int i = 0; i + 1 < kN; ++i) {
         const float top = std::min(ys[i], mid), bottom = std::max(ys[i], mid);
         c.fill(xs[i], top, xs[i + 1] - xs[i], bottom - top);
      }
      c.setColor(color_);
      line(c, xs, ys, kN, std::max(1.5f, h * 0.025f));
   }

protected:
   double signature() const override { return cc(algoId_) * 1000.0 + cc(shapeCc_); }

private:
   static double saw(double p) { return 1.0 - 2.0 * (p - std::floor(p)); }
   static double tri(double p) {
      p -= std::floor(p);
      return p < 0.5 ? -1.0 + 4.0 * p : 3.0 - 4.0 * p;
   }
   double sample(int algo, double shape, double p) const {
      constexpr double kTau = 6.283185307179586;
      const double sn = std::sin(kTau * p);
      if (fm_ && algo >= 12) { // FM sine, triangle, saw, square
         const double depth = shape * 4.0, ph = p + depth * std::sin(kTau * p * 2.0) / kTau;
         switch (algo - 12) {
         case 0: return std::sin(kTau * ph);
         case 1: return tri(ph + 0.25);
         case 2: return saw(ph + 0.5);
         default: return std::sin(kTau * ph) >= 0 ? 0.8 : -0.8;
         }
      }
      if (!fm_ && algo >= 12) { // wavetables
         const int table = algo - 12;
         double v = 0;
         for (int k = 1; k <= 6; ++k) {
            const double weight = std::cos(shape * 3.0 + k * (0.7 + 0.31 * table)) / k;
            v += weight * std::sin(kTau * p * k + table * 0.4 * k);
         }
         return v * 0.9;
      }
      switch (algo) {
      case 0: return std::sin(kTau * p + shape * 2.0 * sn);                 // sine, folded by shape
      case 1: return std::fabs(sn) * 2.0 - 1.0 + shape * 0.3 * sn;          // sine full (rectified)
      case 2: return sn > 0 ? sn * 2.0 - 1.0 : -1.0 + shape * 0.4;          // sine half
      case 3: return tri(p + 0.25) > 1.0 - 2.0 * shape ? 0.9 : tri(p + 0.25); // tri > pulse
      case 4: {                                                              // tri > saw
         const double k = 0.5 + 0.49 * shape, q = p - std::floor(p);
         return q < k ? -1.0 + 2.0 * q / k : 1.0 - 2.0 * (q - k) / (1.0 - k);
      }
      case 5: return std::clamp(tri(p + 0.25) * (1.0 + shape * 8.0), -0.9, 0.9); // tri > square
      case 6: return saw(p) * 0.9;
      case 7:
      case 8: { // supersaws: detuned saws, wider with shape
         const double spread = 0.02 + shape * (algo == 7 ? 0.08 : 0.16);
         return (saw(p) + saw(p * (1.0 + spread) + 0.3) + saw(p * (1.0 - spread) + 0.6)) * 0.35;
      }
      case 9: return saw(p * (1.0 + shape * 3.0)) * 0.9;  // synced saw
      case 10: return (p - std::floor(p)) < 0.5 - shape * 0.45 ? 0.8 : -0.8; // square > pulse
      case 11: return std::sin(kTau * p * (1.0 + shape * 3.0)) >= 0 ? 0.8 : -0.8; // synced square
      default: return sn;
      }
   }

   unsigned color_;
   int algoId_, shapeCc_;
   bool fm_;
};

// An LFO's wave over two cycles, from its wave setting (read by CLAP id).
class LfoShape : public Picture {
public:
   LfoShape(CcValue value, unsigned color, int waveId) : Picture(std::move(value)), color_(color), waveId_(waveId) {}

   void draw(visage::Canvas &c) override {
      inset(c);
      const float w = width(), h = height(), mid = h * 0.5f, amp = h * 0.34f;
      const int wave = static_cast<int>(std::lround(cc(waveId_)));
      const bool unipolar = wave >= 6 && wave != 4 && wave != 5;
      const float base = unipolar ? mid + amp : mid;
      c.setColor(withAlpha(glassTheme().panelBorder, 0x50));
      c.fill(0, base, w, 1.0f);
      constexpr int kN = 120;
      float xs[kN], ys[kN];
      for (int i = 0; i < kN; ++i) {
         const double p = 2.0 * i / (kN - 1);
         xs[i] = 4.0f + (w - 8.0f) * static_cast<float>(i) / (kN - 1);
         const double v = value(wave, p);
         ys[i] = unipolar ? mid + amp - 2.0f * amp * static_cast<float>(v) : mid - amp * static_cast<float>(v);
      }
      c.setColor(color_);
      line(c, xs, ys, kN, std::max(1.5f, h * 0.03f));
   }

protected:
   double signature() const override { return cc(waveId_); }

private:
   // -1..1 for the bipolar waves, 0..1 for the unipolar ones, at phase p (cycles).
   static double value(int wave, double p) {
      const double q = p - std::floor(p);
      const int cycle = static_cast<int>(p);
      const auto noise = [](int k) { return std::fmod(std::sin(k * 12.9898 + 4.1) * 43758.5453, 1.0); };
      const double held = noise(static_cast<int>(p * 4.0));
      switch (wave) {
      case 0: return std::sin(6.283185307179586 * q);
      case 1: return q < 0.25 ? 4 * q : q < 0.75 ? 2 - 4 * q : 4 * q - 4;
      case 2: return 1.0 - 2.0 * q;
      case 3: return q < 0.5 ? 1.0 : -1.0;
      case 4: return held;                                        // S&H bipolar
      case 5: return std::sin(p * 9.0 + noise(cycle) * 6) * 0.8; // random, smooth
      case 6: case 7: case 8: return std::pow(q, 0.5 + (wave - 6) * 1.2);            // attacks
      case 9: case 10: case 11: return std::pow(1.0 - q, 0.5 + (wave - 9) * 1.2);    // decays
      case 12: case 13: case 14: {                                                   // attack-decays
         const double k = 0.5 + (wave - 13) * 0.3;
         return q < k ? q / k : (1.0 - q) / (1.0 - k);
      }
      case 15: return q < 0.5 ? 1.0 : 0.0;
      case 16: return q < 0.5 ? 0.0 : 1.0;
      case 17: return std::fabs(held);
      default: return 0.5 + 0.4 * std::sin(p * 9.0 + noise(cycle) * 6);
      }
   }

   unsigned color_;
   int waveId_;
};

// A darker rounded box to group a few widgets inside a section.
class Tile : public visage::Frame {
public:
   Tile() { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      c.setColor(0x40141a26);
      c.roundedRectangle(0, 0, width(), height(), 6);
      c.setColor(0x1effffff);
      c.roundedRectangleBorder(0, 0, width(), height(), 6, 1.0f);
   }
};

// A coloured outline pill with a source's name, or a dim dash when the slot has no source.
class Chip : public visage::Frame {
public:
   Chip() { setIgnoresMouseEvents(true, false); }
   void set(std::string text, unsigned color) {
      if (text == text_ && color == color_)
         return;
      text_ = std::move(text);
      color_ = color;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float h = height();
      if (text_.empty()) {
         c.setColor(glassTheme().dimText);
         c.text("\xE2\x80\x94", ui::font(h * 0.6f), visage::Font::kLeft, 2, 0, width(), h);
         return;
      }
      const visage::Font f = ui::font(h * 0.6f);
      const float tw = std::min(width(), ui::font(h * 0.6f, dpiScale()).stringWidth(visage::String(text_).toUtf32()) + h);
      c.setColor(color_);
      c.roundedRectangleBorder(0, 0, tw, h, h * 0.5f, 1.0f);
      c.text(text_, f, visage::Font::kCenter, 0, 0, tw, h);
   }

private:
   std::string text_;
   unsigned color_ = 0;
};

// An arrow from a slot's source to its destination.
class Arrow : public visage::Frame {
public:
   Arrow() { setIgnoresMouseEvents(true, false); }
   void setColor(unsigned color) {
      if (color != color_) {
         color_ = color;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), m = h * 0.5f, t = std::max(1.2f, h * 0.09f);
      c.setColor(color_);
      c.segment(1, m, w - 2, m, t, true);
      c.segment(w - 2 - m * 0.6f, m - m * 0.6f, w - 2, m, t, true);
      c.segment(w - 2 - m * 0.6f, m + m * 0.6f, w - 2, m, t, true);
   }

private:
   unsigned color_ = 0xff8d939c;
};

// The mod wheel drawn as a wheel seen from the front: ridges that roll with the value and a
// coloured line where it sits. Drag it up and down like a vertical slider.
class WheelStrip : public ui::Slider {
public:
   explicit WheelStrip(ui::ValueRange range) : ui::Slider(ui::Slider::Orientation::Vertical, range) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = glassTheme();
      const float w = width(), h = height(), r = w * 0.3f;
      c.setColor(visage::Brush::linear(visage::Color(t.knobLight), visage::Color(t.knobDark), visage::Point(0, 0),
                                       visage::Point(w, 0)));
      c.roundedRectangle(0, 0, w, h, r);
      c.setColor(t.knobEdge);
      c.roundedRectangleBorder(0, 0, w, h, r, 1.0f);
      const float f = fraction(value());
      const float roll = f * 9.0f * w * 0.25f;
      c.setColor(withAlpha(0xff000000, 0x70));
      for (float y = std::fmod(roll, w * 0.25f); y < h - 4; y += w * 0.25f)
         if (y > 4)
            c.fill(w * 0.18f, y, w * 0.64f, 1.0f);
      const float y = h - 6 - f * (h - 12);
      c.setColor(accent());
      c.roundedRectangle(2, y - 2.5f, w - 4, 5, 2.5f);
   }
};

} // namespace mm2::visuals
