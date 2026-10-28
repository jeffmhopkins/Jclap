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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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
      const ui::Theme &t = ui::defaultTheme();
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

// A darker rounded box to group a few widgets inside a section.
class Tile : public visage::Frame {
public:
   Tile() { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      c.setColor(0xff191c22);
      c.roundedRectangle(0, 0, width(), height(), 4);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 4, 1.0f);
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
         c.setColor(ui::defaultTheme().dimText);
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
      const ui::Theme &t = ui::defaultTheme();
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
