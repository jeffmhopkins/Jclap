#pragma once

// The panel's pictures: small drawings made only from the knobs' CC values (and, for the envelopes,
// the notes going through the plugin). The plugin can't hear the synth, and its switches (filter
// modes, LFO waves, the matrix routing) aren't on MIDI, so none of them claim to show the sound; they
// are guides to what the numbers mean. Only editor.cpp includes this.

#include <jclap/ui/slider.h>
#include <jclap/ui/theme.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <jclap/ui/design_frame.h>
#include <visage/ui.h>

namespace mb::visuals {

namespace ui = jclap::ui;

// A CC's current 0-127 value, by CC number.
using CcValue = std::function<double(int cc)>;

// The notes going through the plugin, for the envelopes' moving dots: whether a key is down, and
// the seconds since the last note started and since the last key went up (< 0 if none yet).
struct Gate {
   bool held = false;
   double sinceOn = -1, sinceOff = -1;
};
using GateSource = std::function<Gate()>;

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

// Both filters' low-pass responses on one plot, the Steiner's and the ladder's in their colours,
// each from its cutoff and resonance offset by Master Cutoff (CC 27, centred), with dashed curves
// for where ENV 1 takes each cutoff at its peak. The modes, slopes and routing are buttons on the
// synth, so this is a guide, not a measurement.
class DualFilter : public Picture {
public:
   struct Filter {
      int cutoff, resonance, envAmount;
      unsigned color;
   };
   DualFilter(CcValue value, Filter a, Filter b) : Picture(std::move(value)), filters_{a, b} {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(110.0f), w = width(), h = height(), pad = 6 * k;
      inset(c);
      constexpr int kPoints = 120;
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
      float xs[kPoints];
      for (int i = 0; i < kPoints; ++i)
         xs[i] = pad + (w - 2 * pad) * i / (kPoints - 1);
      for (const Filter &fl : filters_) {
         const float cutoff = std::clamp(static_cast<float>(cc(fl.cutoff) + (cc(27) - 64.0)), 0.0f, 127.0f);
         const float peak = std::clamp(cutoff + (static_cast<float>(cc(fl.envAmount)) - 64.0f) * 0.9f, 0.0f, 127.0f);
         const float q = 0.6f + 6.0f * f(fl.resonance);
         float ys[kPoints], ye[kPoints];
         const auto curve = [&](float cut, float *out) {
            const float fc = 20.0f * std::pow(1000.0f, cut / 127.0f);
            for (int i = 0; i < kPoints; ++i) {
               const float hz = 20.0f * std::pow(1000.0f, i / float(kPoints - 1)), x = hz / fc;
               const float m = 1.0f / std::sqrt((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
               out[i] = yOf(20.0f * std::log10(m * m));
            }
         };
         curve(cutoff, ys);
         curve(peak, ye);
         c.setColor(withAlpha(fl.color, 0x22));
         fillUnder(c, xs, ys, kPoints, h - 1);
         c.setColor(withAlpha(fl.color, 0x90));
         line(c, xs, ye, kPoints, 1.0f * k, 3);
         c.setColor(fl.color);
         line(c, xs, ys, kPoints, 1.8f * k);
      }
   }

protected:
   double signature() const override {
      return sum({filters_[0].cutoff, filters_[0].resonance, filters_[0].envAmount, 27}) +
             1e9 * sum({filters_[1].cutoff, filters_[1].resonance, filters_[1].envAmount});
   }

private:
   Filter filters_[2];
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
   EnvShape(CcValue value, int delay, int attack, int decay, int sustain, int release, unsigned color, GateSource gate = {})
      : Picture(std::move(value)), d_(delay), a_(attack), dc_(decay), s_(sustain), r_(release), color_(color),
        gate_(std::move(gate)) {}
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = s(90.0f), w = width(), h = height(), pad = 8 * k;
      inset(c);
      float xs[6], ys[6];
      corners(xs, ys);
      const float floor = h - pad;
      c.setColor(t.track);
      c.fill(pad, floor, w - 2 * pad, 1);
      c.setColor(withAlpha(color_, 0x38));
      fillUnder(c, xs, ys, 6, floor);
      c.setColor(color_);
      line(c, xs, ys, 6, 1.8f * k);
      for (int i = 2; i <= 4; ++i)
         c.circle(xs[i] - 3 * k, ys[i] - 3 * k, 6 * k);
      // Where a note has got to, as a bright dot on the curve.
      const float at = position();
      if (at >= 0) {
         const int seg = std::min(4, static_cast<int>(at));
         const float frac = at - seg, x = xs[seg] + (xs[seg + 1] - xs[seg]) * frac,
                     y = ys[seg] + (ys[seg + 1] - ys[seg]) * frac;
         c.setColor(0xffffffff);
         c.circle(x - 5 * k, y - 5 * k, 10 * k);
      }
   }

protected:
   double signature() const override {
      // The dot moves without any knob moving, so its place is part of what's drawn.
      return sum({d_ < 0 ? a_ : d_, a_, dc_, s_, r_}) + 1e11 * std::round(position() * 200.0f + 1.0f);
   }

private:
   // The curve's corners: start, end of delay, peak, end of decay, end of the held sustain, end of
   // release. The segments are drawn wider for longer times, but not to scale.
   void corners(float *xs, float *ys) const {
      const float k = height() / 90.0f, w = width(), h = height(), pad = 8 * k, floor = h - pad, top = pad + 4 * k;
      const auto span = [&](int n) { return n < 0 ? 0.0f : 0.04f + std::sqrt(f(n)); };
      const float spans[] = {span(d_), span(a_), span(dc_), 0.5f, span(r_)};
      float total = 0;
      for (float sp : spans)
         total += sp;
      const float unit = (w - 2 * pad) / total, sus = floor - (floor - top) * f(s_);
      const float levels[6] = {floor, floor, top, sus, sus, floor};
      float x = pad;
      for (int i = 0; i < 6; ++i) {
         if (i > 0)
            x += spans[i - 1] * unit;
         xs[i] = x;
         ys[i] = levels[i];
      }
   }

   // A stage's time in seconds for its slider. Arturia doesn't publish the curve; this guesses 1 ms
   // to 10 s, evenly in log time.
   double seconds(int n) const { return n < 0 ? 0.0 : 0.001 * std::pow(10000.0, f(n)); }

   // Where a note is along the curve, as segment index plus fraction (0-5), or -1 for no note: it
   // runs through delay, attack and decay, waits at the sustain while a key is down, and runs the
   // release once the keys are up.
   float position() const {
      if (!gate_)
         return -1;
      const Gate g = gate_();
      if (g.held && g.sinceOn >= 0) {
         double t = g.sinceOn;
         const double stages[3] = {seconds(d_), seconds(a_), seconds(dc_)};
         for (int i = 0; i < 3; ++i) {
            if (t < stages[i])
               return static_cast<float>(i + t / stages[i]);
            t -= stages[i];
         }
         return 3.0f;
      }
      if (!g.held && g.sinceOff >= 0 && g.sinceOff < seconds(r_))
         return static_cast<float>(4.0 + g.sinceOff / seconds(r_));
      return -1;
   }

   int d_, a_, dc_, s_, r_;
   unsigned color_;
   GateSource gate_;
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

// A section box drawn like the synth's: a filled panel (dark for the sound sources and filters,
// grey for the modulation), a rule across the top in its colour, and the title on a tab in the
// middle of the rule.
class HwSection : public visage::Frame {
public:
   HwSection(std::string title, bool dark, unsigned rule = 0xffc9d0d8)
      : title_(std::move(title)), dark_(dark), rule_(rule) {
      setIgnoresMouseEvents(true, false);
   }
   void draw(visage::Canvas &c) override {
      const float k = ui::designScale(*this), w = width(), h = height();
      c.setColor(dark_ ? 0xff15181c : 0xff272c33);
      c.roundedRectangle(0, 5 * k, w, h - 5 * k, 3 * k);
      c.setColor(rule_);
      c.fill(0, 5 * k, w, 2.5f * k);
      if (title_.empty())
         return;
      const float size = 11.5f * k;
      const float tw = ui::font(size, dpiScale()).stringWidth(visage::String(title_).toUtf32()) + 28 * k;
      const float tx = (w - tw) * 0.5f;
      c.fill(tx + 4 * k, 0, tw - 8 * k, 17 * k);
      c.triangle(tx, 0, tx + 4 * k, 0, tx + 4 * k, 17 * k);
      c.triangle(tx + tw, 0, tx + tw - 4 * k, 0, tx + tw - 4 * k, 17 * k);
      c.setColor(0xff1a1d22);
      c.text(title_, ui::font(size), visage::Font::kCenter, tx, 0, tw, 17 * k);
   }

private:
   std::string title_;
   bool dark_;
   unsigned rule_;
};

// The preset display: three red seven-segment-style characters, as the synth shows "A01".
class PresetDisplay : public visage::Frame {
public:
   PresetDisplay() { setIgnoresMouseEvents(true, false); }
   void setText(std::string text) {
      if (text != text_) {
         text_ = std::move(text);
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height();
      c.setColor(0xff0b0c0e);
      c.roundedRectangle(0, 0, w, h, 3);
      c.setColor(withAlpha(0xffff4a3a, 0x28));
      c.text("888", ui::font(h * 0.78f), visage::Font::kRight, 0, 0, w - h * 0.25f, h);
      c.setColor(0xffff5a48);
      c.text(text_, ui::font(h * 0.78f), visage::Font::kRight, 0, 0, w - h * 0.25f, h);
   }

private:
   std::string text_ = "---";
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
