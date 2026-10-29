#pragma once

// The panel's pictures: small drawings made only from the knobs' CC values. The plugin can't hear the
// synth, and its switches (filter modes, LFO waves, the matrix routing) aren't on MIDI, so none of
// them claim to show the sound; they are guides to what the numbers mean. Only editor.cpp includes this.

#include <jclap/ui/slider.h>
#include <jclap/ui/theme.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <visage/ui.h>

namespace mb::visuals {

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
   float f(int n) const { return static_cast<float>(cc(n) / 127.0); }
   // Design units: these pictures are drawn for their design height.
   float s(float designHeight) const { return height() / designHeight; }
   // A checksum of some CCs' values, for refresh().
   double sum(std::initializer_list<int> ccs) const {
      double total = 0.0, weight = 1.0;
      for (int n : ccs) {
         total += cc(n) * weight;
         weight *= 131.0;
      }
      return total;
   }

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
            c.fill(x, std::min(y, floor), std::min(1.0f, xs[i + 1] - x), std::fabs(floor - y));
         }
      }
   }
   static void line(visage::Canvas &c, const float *xs, const float *ys, int n, float thick, int dash = 0) {
      for (int i = 0; i + 1 < n; ++i)
         if (!dash || (i / dash) % 2 == 0)
            c.segment(xs[i], ys[i], xs[i + 1], ys[i + 1], thick, true);
   }
   static void note(visage::Canvas &c, const std::string &text, float k, float w) {
      c.setColor(ui::defaultTheme().dimText);
      c.text(text, ui::font(9 * k), visage::Font::kRight, 0, 3 * k, w - 8 * k, 12 * k);
   }

private:
   CcValue value_;
   double shown_ = -1.0;
};

// Two cycles of a VCO's wave mix: sub, saw, square and triangle at their levels, with the square's
// pulse width, the ultrasaw's thickening and the metalizer's folding roughly applied. The sub
// octave's own waveform is a knob too, but its shapes aren't published, so it's drawn as a square.
class VcoWave : public Picture {
public:
   struct Ccs {
      int sub, saw, square, triangle, ultrasaw, pw, metal;
   };
   VcoWave(CcValue value, Ccs ccs, unsigned color) : Picture(std::move(value)), ccs_(ccs), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(44.0f), w = width(), h = height(), pad = 6 * k, mid = h * 0.5f;
      inset(c);
      c.setColor(t.track);
      c.fill(pad, mid, w - 2 * pad, 1);
      constexpr int kPoints = 200;
      float xs[kPoints], ys[kPoints];
      const float sub = f(ccs_.sub), saw = f(ccs_.saw), sq = f(ccs_.square), tri = f(ccs_.triangle);
      const float pw = 0.5f + 0.42f * (f(ccs_.pw) - 0.5f) * 2.0f, ultra = f(ccs_.ultrasaw), metal = f(ccs_.metal);
      const float total = std::max(1.0f, sub + saw + sq + tri);
      for (int i = 0; i < kPoints; ++i) {
         const float p = 2.0f * i / (kPoints - 1), ph = p - std::floor(p);
         float sawV = 1.0f - 2.0f * ph;
         sawV = (1.0f - ultra * 0.5f) * sawV + ultra * 0.5f * (1.0f - 2.0f * std::fmod(ph + 0.33f, 1.0f));
         const float sqV = ph < pw ? 1.0f : -1.0f;
         float triV = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
         triV = std::sin(triV * (1.0f + 3.0f * metal) * 1.5708f);
         const float subV = p < 1.0f ? 1.0f : -1.0f;
         const float v = (sub * subV + saw * sawV + sq * sqV + tri * triV) / total;
         xs[i] = pad + (w - 2 * pad) * i / (kPoints - 1);
         ys[i] = mid - v * (mid - pad);
      }
      c.setColor(withAlpha(color_, 0x30));
      fillUnder(c, xs, ys, kPoints, mid);
      c.setColor(color_);
      line(c, xs, ys, kPoints, 1.6f * k);
      if (sub + saw + sq + tri <= 0.0f)
         note(c, "ALL WAVE LEVELS AT 0", k, w);
   }

protected:
   double signature() const override {
      return sum({ccs_.sub, ccs_.saw, ccs_.square, ccs_.triangle}) + 1e9 * sum({ccs_.ultrasaw, ccs_.pw, ccs_.metal});
   }

private:
   Ccs ccs_;
   unsigned color_;
};

// A low-pass response from a filter's cutoff and resonance, offset by Master Cutoff (CC 27,
// centred), with a dashed second curve for where ENV 1 takes the cutoff at its peak. Each filter's
// mode and slope are buttons on the synth, so this is a guide, not a measurement.
class FilterCurve : public Picture {
public:
   FilterCurve(CcValue value, int cutoff, int resonance, int envAmount, unsigned color)
      : Picture(std::move(value)), cutoff_(cutoff), res_(resonance), env_(envAmount), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(120.0f), w = width(), h = height(), pad = 6 * k;
      inset(c);
      constexpr int kPoints = 140;
      const auto yOf = [&](float db) { return pad + (h - 2 * pad) * (18.0f - std::clamp(db, -48.0f, 18.0f)) / 66.0f; };
      for (float hz : {100.0f, 1000.0f, 10000.0f}) {
         const float x = pad + (w - 2 * pad) * std::log(hz / 20.0f) / std::log(1000.0f);
         c.setColor(t.track);
         c.fill(x, 0, 1, h);
         c.setColor(t.dimText);
         c.text(hz < 1000 ? "100" : hz < 10000 ? "1k" : "10k", ui::font(9 * k), visage::Font::kLeft, x + 3 * k,
                h - 15 * k, 40 * k, 12 * k);
      }
      c.setColor(t.track);
      c.fill(0, yOf(0), w, 1);

      const float cutoff = std::clamp(static_cast<float>(cc(cutoff_) + (cc(27) - 64.0)), 0.0f, 127.0f);
      const float peak = std::clamp(cutoff + (static_cast<float>(cc(env_)) - 64.0f) * 0.9f, 0.0f, 127.0f);
      const float q = 0.6f + 6.0f * f(res_);
      float xs[kPoints], ys[kPoints], ye[kPoints];
      const auto curve = [&](float cut, float *out) {
         const float fc = 20.0f * std::pow(1000.0f, cut / 127.0f);
         for (int i = 0; i < kPoints; ++i) {
            const float hz = 20.0f * std::pow(1000.0f, i / float(kPoints - 1)), x = hz / fc;
            const float m = 1.0f / std::sqrt((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
            out[i] = yOf(20.0f * std::log10(m * m));
         }
      };
      for (int i = 0; i < kPoints; ++i)
         xs[i] = pad + (w - 2 * pad) * i / (kPoints - 1);
      curve(cutoff, ys);
      curve(peak, ye);
      c.setColor(withAlpha(color_, 0x30));
      fillUnder(c, xs, ys, kPoints, h - 1);
      c.setColor(withAlpha(0xffe6e8eb, 0x90));
      line(c, xs, ye, kPoints, 1.2f * k, 3);
      c.setColor(color_);
      line(c, xs, ys, kPoints, 2.0f * k);
      const float hx = pad + (w - 2 * pad) * cutoff / 127.0f;
      const float hy = *std::min_element(ys, ys + kPoints);
      c.setColor(withAlpha(color_, 0x40));
      c.circle(hx - 9 * k, hy - 9 * k, 18 * k);
      c.setColor(color_);
      c.circle(hx - 6 * k, hy - 6 * k, 12 * k);
   }

protected:
   double signature() const override { return sum({cutoff_, res_, env_, 27}); }

private:
   int cutoff_, res_, env_;
   unsigned color_;
};

// The mixer's five levels side by side, as wide as they are loud.
class MixBar : public Picture {
public:
   MixBar(CcValue value, const int (&ccs)[5], const unsigned (&colors)[5]) : Picture(std::move(value)) {
      std::copy(ccs, ccs + 5, ccs_);
      std::copy(colors, colors + 5, colors_);
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float w = width(), h = height();
      double total = 0;
      for (int n : ccs_)
         total += cc(n);
      c.setColor(t.track);
      c.roundedRectangle(0, 0, w, h, 2);
      if (total <= 0)
         return;
      float x = 0;
      for (int i = 0; i < 5; ++i) {
         const float part = static_cast<float>(cc(ccs_[i]) / total) * w;
         if (part >= 1.0f) {
            c.setColor(colors_[i]);
            c.fill(x, 0, std::max(1.0f, part - 2.0f), h);
         }
         x += part;
      }
   }

protected:
   double signature() const override { return sum({ccs_[0], ccs_[1], ccs_[2], ccs_[3], ccs_[4]}); }

private:
   int ccs_[5] = {};
   unsigned colors_[5] = {};
};

// An envelope's shape from its sliders: an optional delay, then attack, decay, sustain and release,
// with the times on a square-root curve so short ones still show.
class EnvShape : public Picture {
public:
   // delay < 0 for an envelope without one.
   EnvShape(CcValue value, int delay, int attack, int decay, int sustain, int release, unsigned color)
      : Picture(std::move(value)), d_(delay), a_(attack), dc_(decay), s_(sustain), r_(release), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(90.0f), w = width(), h = height(), pad = 8 * k, floor = h - pad, top = pad + 4 * k;
      inset(c);
      const auto time = [&](int n) { return n < 0 ? 0.0f : 0.04f + std::sqrt(f(n)); };
      const float hold = 0.5f;
      const float spans[] = {time(d_), time(a_), time(dc_), hold, time(r_)};
      float total = 0;
      for (float sp : spans)
         total += sp;
      const float unit = (w - 2 * pad) / total, sus = floor - (floor - top) * f(s_);
      float xs[6], ys[6];
      float x = pad;
      xs[0] = x, ys[0] = floor;
      x += spans[0] * unit, xs[1] = x, ys[1] = floor;
      x += spans[1] * unit, xs[2] = x, ys[2] = top;
      x += spans[2] * unit, xs[3] = x, ys[3] = sus;
      x += spans[3] * unit, xs[4] = x, ys[4] = sus;
      x += spans[4] * unit, xs[5] = x, ys[5] = floor;
      c.setColor(t.track);
      c.fill(pad, floor, w - 2 * pad, 1);
      c.setColor(withAlpha(color_, 0x38));
      fillUnder(c, xs, ys, 6, floor);
      c.setColor(color_);
      line(c, xs, ys, 6, 1.8f * k);
      for (int i = 2; i <= 4; ++i)
         c.circle(xs[i] - 3 * k, ys[i] - 3 * k, 6 * k);
   }

protected:
   double signature() const override { return sum({d_ < 0 ? a_ : d_, a_, dc_, s_, r_}); }

private:
   int d_, a_, dc_, s_, r_;
   unsigned color_;
};

// The analog effect's echoes: spaced by Delay Time (CC 112), each one quieter by Regeneration
// (CC 113), as loud as Dry/Wet (CC 31). The effect's mode is a button on the synth, so for the
// chorus, flanger and the others this only shows the knobs.
class FxTaps : public Picture {
public:
   FxTaps(CcValue value, unsigned color) : Picture(std::move(value)), color_(color) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(90.0f), w = width(), h = height(), pad = 10 * k, floor = h - pad;
      inset(c);
      const float wet = f(31);
      const unsigned color = wet > 0 ? color_ : withAlpha(color_, 0x70);
      c.setColor(t.track);
      c.fill(pad, floor, w - 2 * pad, 1);
      // The dry signal, then the wet echoes.
      c.setColor(t.dimText);
      c.fill(pad + 2 * k, pad + 10 * k, 3 * k, floor - pad - 10 * k);
      const float spacing = (10 + 50 * f(112)) * k, fb = f(113);
      float level = std::max(wet, 0.08f);
      for (float x = pad + 2 * k + spacing; x < w - pad && level > 0.04f; x += spacing) {
         const float bar = (floor - pad - 10 * k) * level;
         c.setColor(color);
         c.fill(x, floor - bar, 3 * k, bar);
         level *= fb * 0.92f;
         if (fb <= 0.0f)
            break;
      }
      // Width/Depth as a stereo spread under the echoes.
      const float spread = (w - 2 * pad) * 0.5f * f(115);
      c.setColor(withAlpha(color, 0x60));
      c.fill(w * 0.5f - spread, floor + 3 * k, 2 * spread, 2 * k);
      if (wet <= 0)
         note(c, "DRY/WET 0 \xC2\xB7 NOT HEARD", k, w);
   }

protected:
   double signature() const override { return sum({31, 112, 113, 115}); }

private:
   unsigned color_;
};

// The 16 x 16 matrix as a map of its rows (the sources, A to P) and columns. Its routing isn't on
// MIDI, so the cells are drawn empty; the row and column heads say what each one is on the synth.
class MatrixMap : public visage::Frame {
public:
   MatrixMap() { setIgnoresMouseEvents(true, false); }
   static constexpr const char *kSources[16] = {"ENV 1", "ENV 2",      "ENV 3",      "ENV FOLLOW", "LFO 1",       "LFO 2",
                                                "LFO 3", "MOD WHEEL",  "KBD / SEQ",  "AFTERTOUCH", "VELOCITY",    "SEQ MOD",
                                                "M1",    "M2",         "EXP 1 / M3", "EXP 2 / M4"};
   void setAccents(unsigned rows, unsigned custom) {
      rowColor_ = rows;
      customColor_ = custom;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float w = width(), h = height(), k = h / 400.0f;
      const float labelW = 112 * k, headH = 24 * k, footH = 12 * k;
      const float cell = std::min((w - labelW) / 16.0f, (h - headH - footH) / 16.0f);
      const float gx = labelW, gy = headH;
      const auto small = ui::font(9.5f * k);
      for (int col = 0; col < 16; ++col) {
         c.setColor(col >= 12 ? customColor_ : t.dimText);
         c.text(std::to_string(col + 1), small, visage::Font::kCenter, gx + col * cell, 4 * k, cell, 14 * k);
      }
      for (int row = 0; row < 16; ++row) {
         const float y = gy + row * cell;
         c.setColor(rowColor_);
         c.text(std::string(1, static_cast<char>('A' + row)), small, visage::Font::kLeft, 2 * k, y, 14 * k, cell);
         c.setColor(t.text);
         c.text(kSources[row], small, visage::Font::kLeft, 18 * k, y, labelW - 20 * k, cell);
         for (int col = 0; col < 16; ++col) {
            const float x = gx + col * cell, m = cell * 0.18f;
            c.setColor(col >= 12 ? withAlpha(customColor_, 0x30) : 0xff262b33);
            c.roundedRectangle(x + m, y + m, cell - 2 * m, cell - 2 * m, cell * 0.12f);
         }
      }
      c.setColor(t.dimText);
      c.text("COLUMNS 1-12: FIXED DESTINATIONS \xC2\xB7 13-16: ASSIGNABLE BANKS", small, visage::Font::kLeft, gx,
             gy + 16 * cell + 4 * k, 16 * cell, footH);
   }

private:
   unsigned rowColor_ = 0xff8d939c, customColor_ = 0xff8d939c;
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
      const float fr = fraction(value());
      const float roll = fr * 9.0f * w * 0.25f;
      c.setColor(withAlpha(0xff000000, 0x70));
      for (float y = std::fmod(roll, w * 0.25f); y < h - 4; y += w * 0.25f)
         if (y > 4)
            c.fill(w * 0.18f, y, w * 0.64f, 1.0f);
      const float y = h - 6 - fr * (h - 12);
      c.setColor(accent());
      c.roundedRectangle(2, y - 2.5f, w - 4, 5, 2.5f);
   }
};

} // namespace mb::visuals
