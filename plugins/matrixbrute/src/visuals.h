#pragma once

// The panel's pictures: small drawings made from the knobs' CC values (and, for the envelopes, the
// notes going through the plugin), and the synth's buttons and matrix drawn as indicators. The
// plugin can't hear the synth's switches (filter modes, LFO waves, the matrix routing) over MIDI, so
// the indicators start unknown and dim, with a state a later preset read can set; the pictures are
// guides to what the numbers mean. Only editor.cpp includes this.

#include <jclap/ui/slider.h>
#include <jclap/ui/theme.h>

#include <algorithm>
#include <array>
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
// `a` moved `amount` (0 to 1) of the way to `b`, opaque.
inline unsigned mixColor(unsigned a, unsigned b, float amount) {
   unsigned out = 0xff000000;
   for (int shift = 0; shift <= 16; shift += 8) {
      const float x = (a >> shift) & 0xff, y = (b >> shift) & 0xff;
      out |= static_cast<unsigned>(x + (y - x) * amount + 0.5f) << shift;
   }
   return out;
}
// A section's label colour: its accent lifted toward white, so it reads on the dark and grey panels.
inline unsigned labelTint(unsigned accent) { return mixColor(accent, 0xffffffff, 0.3f); }
// The same label while its control's setting isn't known yet: toward the dim grey.
inline unsigned dimTint(unsigned tint) { return mixColor(tint, 0xff8d939c, 0.45f); }
// A string's width in `size` type, for fitting text.
inline float textWidth(const visage::Frame &frame, const std::string &text, float size) {
   return ui::font(size, frame.dpiScale()).stringWidth(visage::String(text).toUtf32());
}
// The smallest text anywhere on the panel and the size of control names, in design units.
constexpr float kSmallText = 12.0f, kNameText = 13.5f;

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
   void note(visage::Canvas &c, const std::string &text) const {
      const float d = ui::designScale(*this);
      c.setColor(ui::defaultTheme().dimText);
      c.text(text, ui::font(kSmallText * d), visage::Font::kRight, 0, 4 * d, width() - 8 * d, 15 * d);
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
      // Strokes scale with the smaller of height and width, so a tall picture keeps thin lines.
      const float k = std::min(s(44.0f), width() / 240.0f), w = width(), h = height(), pad = 6 * k, mid = h * 0.5f;
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
         note(c, "ALL WAVE LEVELS AT 0");
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
      const float d = ui::designScale(*this);
      const float k = std::min(s(110.0f), width() / 280.0f), w = width(), h = height(), pad = 6 * k;
      // The curves stay above a strip for the frequency labels.
      const float bottom = h - 18 * d, top = pad + 22 * d;
      inset(c);
      constexpr int kPoints = 160;
      const auto yOf = [&](float db) { return top + (bottom - top) * (18.0f - std::clamp(db, -48.0f, 18.0f)) / 66.0f; };
      const auto small = ui::font(kSmallText * d);
      for (float hz : {100.0f, 1000.0f, 10000.0f}) {
         const float x = pad + (w - 2 * pad) * std::log(hz / 20.0f) / std::log(1000.0f);
         c.setColor(t.track);
         c.fill(x, top - 4 * d, 1, bottom - top + 4 * d);
         c.setColor(t.dimText);
         c.text(hz < 1000 ? "100" : hz < 10000 ? "1k" : "10k", small, visage::Font::kCenter, x - 30 * d, bottom + 1 * d,
                60 * d, 16 * d);
      }
      c.setColor(t.track);
      c.fill(pad, yOf(0), w - 2 * pad, 1);
      c.fill(pad, bottom, w - 2 * pad, 1);
      float xs[kPoints];
      for (int i = 0; i < kPoints; ++i)
         xs[i] = pad + (w - 2 * pad) * i / (kPoints - 1);
      float ys[2][kPoints], ye[2][kPoints];
      for (int n = 0; n < 2; ++n) {
         const Filter &fl = filters_[n];
         const float cutoff = std::clamp(static_cast<float>(cc(fl.cutoff) + (cc(27) - 64.0)), 0.0f, 127.0f);
         const float peak = std::clamp(cutoff + (static_cast<float>(cc(fl.envAmount)) - 64.0f) * 0.9f, 0.0f, 127.0f);
         const float q = 0.6f + 6.0f * f(fl.resonance);
         const auto curve = [&](float cut, float *out) {
            const float fc = 20.0f * std::pow(1000.0f, cut / 127.0f);
            for (int i = 0; i < kPoints; ++i) {
               const float hz = 20.0f * std::pow(1000.0f, i / float(kPoints - 1)), x = hz / fc;
               const float m = 1.0f / std::sqrt((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
               out[i] = yOf(20.0f * std::log10(m * m));
            }
         };
         curve(cutoff, ys[n]);
         curve(peak, ye[n]);
      }
      // Both fills first, faint, then ENV 1's peak as a thin solid line, then the responses on top.
      for (int n = 0; n < 2; ++n) {
         c.setColor(withAlpha(filters_[n].color, 0x33));
         fillUnder(c, xs, ys[n], kPoints, bottom);
      }
      for (int n = 0; n < 2; ++n) {
         c.setColor(withAlpha(filters_[n].color, 0x66));
         line(c, xs, ye[n], kPoints, 1.2f * k);
      }
      for (int n = 0; n < 2; ++n) {
         c.setColor(filters_[n].color);
         line(c, xs, ys[n], kPoints, 1.8f * k);
      }
      // The key: each filter's colour, and the faint line for where ENV 1 takes it.
      const char *names[2] = {"VCF1", "VCF2"};
      float x = pad + 8 * d;
      for (int n = 0; n < 2; ++n) {
         c.setColor(filters_[n].color);
         c.fill(x, pad + 9 * d, 14 * d, 3 * d);
         c.text(names[n], small, visage::Font::kLeft, x + 18 * d, pad + 2 * d, 40 * d, 16 * d);
         x += 18 * d + textWidth(*this, names[n], kSmallText * d) + 14 * d;
      }
      c.setColor(t.dimText);
      c.fill(x, pad + 10 * d, 14 * d, 1.2f * d);
      c.text("ENV 1 PEAK", small, visage::Font::kLeft, x + 18 * d, pad + 2 * d, 100 * d, 16 * d);
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

// One mixer source's level as a meter in its colour, with its share of the whole mix.
class MixMeter : public Picture {
public:
   MixMeter(CcValue value, const int (&ccs)[5], int index, unsigned color) : Picture(std::move(value)), index_(index), color_(color) {
      std::copy(ccs, ccs + 5, ccs_);
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float d = ui::designScale(*this), w = width();
      double total = 0;
      for (int n : ccs_)
         total += cc(n);
      const float level = f(ccs_[index_]);
      c.setColor(t.track);
      c.roundedRectangle(0, 0, w, 8 * d, 3 * d);
      if (level > 0) {
         c.setColor(color_);
         c.roundedRectangle(0, 0, std::max(6 * d, w * level), 8 * d, 3 * d);
      }
      const int share = total > 0 ? static_cast<int>(std::lround(100.0 * cc(ccs_[index_]) / total)) : 0;
      c.setColor(t.dimText);
      c.text(std::to_string(share) + "% OF THE MIX", ui::font(kSmallText * d), visage::Font::kLeft, 0, 13 * d, w, 16 * d);
   }

protected:
   double signature() const override { return sum({ccs_[0], ccs_[1], ccs_[2], ccs_[3], ccs_[4]}); }

private:
   int ccs_[5] = {};
   int index_;
   unsigned color_;
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
      const float k = std::min(s(90.0f), width() / 220.0f), w = width(), h = height(), pad = 8 * k;
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
      const float k = std::min(height() / 90.0f, width() / 220.0f), w = width(), h = height(), pad = 8 * k, floor = h - pad, top = pad + 4 * k;
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
      const float k = std::min(s(90.0f), width() / 320.0f), w = width(), h = height(), pad = 10 * k, floor = h - pad;
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
         note(c, "DRY/WET 0 \xC2\xB7 NOT HEARD");
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
      const float size = 14.0f * k, th = 20 * k;
      const float tw = textWidth(*this, title_, size) + 28 * k;
      const float tx = (w - tw) * 0.5f;
      c.fill(tx + 5 * k, 0, tw - 10 * k, th);
      c.triangle(tx, 0, tx + 5 * k, 0, tx + 5 * k, th);
      c.triangle(tx + tw, 0, tx + tw - 5 * k, 0, tx + tw - 5 * k, th);
      c.setColor(0xff1a1d22);
      c.text(title_, ui::font(size), visage::Font::kCenter, tx, 0, tw, th);
   }

private:
   std::string title_;
   bool dark_;
   unsigned rule_;
};

// A red seven-segment-style display, as the synth shows its preset ("A01") or the sequencer its
// tempo ("100.0"): every place drawn faintly as an 8 (the decimal point as a dot), and the text on
// top, one character to a place.
class PresetDisplay : public visage::Frame {
public:
   // `places` is the faint pattern, e.g. "888" or "888.8".
   explicit PresetDisplay(std::string places = "888") : places_(std::move(places)) { setIgnoresMouseEvents(true, false); }
   void setText(std::string text) {
      if (text != text_) {
         text_ = std::move(text);
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), size = h * 0.74f;
      c.setColor(0xff0b0c0e);
      c.roundedRectangle(0, 0, w, h, 3);
      const auto font = ui::font(size);
      const float digit = textWidth(*this, "8", size) * 1.08f, dot = digit * 0.45f;
      float total = 0;
      for (char p : places_)
         total += p == '.' ? dot : digit;
      // The characters fill the places left to right, skipping a point the text doesn't have.
      std::vector<std::string> shown(places_.size());
      size_t t = 0;
      for (size_t i = 0; i < places_.size() && t < text_.size(); ++i) {
         if (places_[i] == '.' && text_[t] != '.')
            continue;
         shown[i] = std::string(1, text_[t++]);
      }
      float x = (w - total) * 0.5f;
      for (size_t i = 0; i < places_.size(); ++i) {
         const float pw = places_[i] == '.' ? dot : digit;
         c.setColor(withAlpha(0xffff4a3a, 0x28));
         c.text(std::string(1, places_[i]), font, visage::Font::kCenter, x, 0, pw, h);
         if (!shown[i].empty()) {
            c.setColor(0xffff5a48);
            c.text(shown[i], font, visage::Font::kCenter, x, 0, pw, h);
         }
         x += pw;
      }
   }

private:
   std::string places_;
   std::string text_ = "---";
};

// One of the synth's buttons that isn't on MIDI (a filter mode, an LFO wave...), drawn as on the
// hardware: its lights with their names, the button, and its name below. It does nothing when
// clicked. Its state is the lit light (or, for a button without lights, 1 for on), or -1 while it
// isn't known; the plugin can't read these from the synth's presets yet, so they start unknown and
// are drawn dim. setState() is the hook for when they're mapped.
class HwButton : public visage::Frame {
public:
   enum class Lights { Above, Right };
   HwButton(std::string label, std::vector<std::string> lights, Lights place = Lights::Above, unsigned accent = 0xff6fa8ff)
      : label_(std::move(label)), lights_(std::move(lights)), place_(place), accent_(accent) {}
   void setState(int state) {
      if (state != state_) {
         state_ = state;
         redraw();
      }
   }
   int state() const { return state_; }
   // Light i lit by the state's bit i, rather than the state naming one light.
   void setBitLights(bool bits) {
      bitLights_ = bits;
      redraw();
   }
   const std::string &label() const { return label_; }
   // The name's colour (0 for the theme's), e.g. its section's tint.
   void setLabelColor(unsigned color) {
      labelColor_ = color;
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = ui::designScale(*this), w = width(), h = height();
      const bool known = state_ >= 0;
      const unsigned name = labelColor_ ? labelColor_ : t.text;
      const unsigned text = known ? name : (labelColor_ ? dimTint(labelColor_) : t.dimText);
      const unsigned lightText = known ? t.dimText : 0xff7a818b;
      const auto nameFont = ui::font(kNameText * 0.93f * k);
      float widest = 0;
      for (const std::string &l : lights_)
         widest = std::max(widest, textWidth(*this, l, kSmallText * k));
      const auto light = [&](float x, float y, size_t i) {
         const bool lit = known && (bitLights_ ? ((state_ >> i) & 1) != 0 : static_cast<size_t>(state_) == i);
         c.setColor(lit ? accent_ : (known ? 0xff3a4048 : 0xff31363e));
         c.circle(x, y, 7 * k);
      };
      const float bh = 18 * k, nameH = 16 * k;
      if (place_ == Lights::Right && !lights_.empty()) {
         // The button on the left, its lights in a column to its right.
         const float lw = std::min(widest + 13 * k, w - 40 * k), bw = std::min(w - lw - 6 * k, 60 * k);
         const auto small = ui::font(kSmallText * k * std::min(1.0f, (lw - 12 * k) / std::max(widest, 1.0f)));
         const float lh = std::min(16 * k, h / lights_.size()), ly = (h - lh * lights_.size()) * 0.5f;
         for (size_t i = 0; i < lights_.size(); ++i) {
            const float y = ly + i * lh;
            light(w - lw, y + lh * 0.5f - 3.5f * k, i);
            c.setColor(lightText);
            c.text(lights_[i], small, visage::Font::kLeft, w - lw + 11 * k, y, lw - 11 * k, lh);
         }
         const float by = (h - bh - nameH) * 0.5f;
         drawButton(c, 0, by, bw, bh, k, known && lights_.empty() && state_ > 0);
         // The name centred under the button, unless that would cut it off at the left.
         const float nameW = textWidth(*this, label_, kNameText * 0.93f * k) + 4 * k;
         const float ncx = std::max(bw * 0.5f, nameW * 0.5f);
         c.setColor(text);
         c.text(label_, nameFont, visage::Font::kCenter, ncx - nameW * 0.5f, by + bh + 1 * k, nameW, nameH);
         return;
      }
      const float bw = std::min(w - 4 * k, 60 * k);
      float by = (h - bh - nameH) * 0.5f;
      if (!lights_.empty()) {
         const float lw = w / lights_.size();
         // Names too wide for their light's share take turns above and below the row of lights.
         const bool stagger = widest > lw - 2 * k && lights_.size() > 2;
         const float room = stagger ? 2 * lw - 4 * k : lw - 2 * k;
         const auto small = ui::font(kSmallText * k * std::min(1.0f, room / std::max(widest, 1.0f)));
         by = std::max(by, (stagger ? 46 : 30) * k);
         const float top = by - (stagger ? 46 : 30) * k, ledY = top + (stagger ? 18 : 2) * k;
         for (size_t i = 0; i < lights_.size(); ++i) {
            const float cx = lw * (i + 0.5f);
            light(cx - 3.5f * k, ledY, i);
            c.setColor(lightText);
            const float ty = !stagger ? top + 10 * k : (i % 2 == 0 ? top : ledY + 8 * k);
            // Kept inside the button's width, so the outer names aren't cut off.
            const float tx = std::clamp(cx - room * 0.5f, 0.0f, std::max(0.0f, w - room));
            const float tw = textWidth(*this, lights_[i], kSmallText * k) * std::min(1.0f, room / std::max(widest, 1.0f));
            const float nx = std::clamp(cx - tw * 0.5f, 0.0f, std::max(0.0f, w - tw)) - tx;
            c.text(lights_[i], small, visage::Font::kLeft, tx + nx, ty, room, 15 * k);
         }
      }
      drawButton(c, (w - bw) * 0.5f, by, bw, bh, k, known && lights_.empty() && state_ > 0);
      // The name under the button, made a little smaller if the button's width can't hold it.
      const float nameSize = kNameText * 0.93f * k, nameW = textWidth(*this, label_, nameSize);
      c.setColor(text);
      c.text(label_, ui::font(nameSize * std::min(1.0f, (w - 2 * k) / std::max(nameW, 1.0f))), visage::Font::kCenter, 0,
             by + bh + 1 * k, w, nameH);
   }

private:
   void drawButton(visage::Canvas &c, float x, float y, float w, float h, float k, bool lit) const {
      c.setColor(0xff0d0f12);
      c.roundedRectangle(x, y + 1.5f * k, w, h, 3 * k);
      c.setColor(0xff1f2329);
      c.roundedRectangle(x, y, w, h - 1 * k, 3 * k);
      c.setColor(lit ? accent_ : 0xff30353d);
      c.fill(x + 6 * k, y + (lit ? 7 : 2) * k, w - 12 * k, (lit ? 2 : 1) * k);
   }

   std::string label_;
   std::vector<std::string> lights_;
   Lights place_;
   unsigned accent_;
   unsigned labelColor_ = 0;
   int state_ = -1;
   bool bitLights_ = false;
};

// An LFO's custom shape (drawn on the synth's matrix in LFO Draw mode): its 16 steps, -1 to 1, as
// a stepped line. Nothing until a preset read off the synth gives the steps; dim unless the LFO's
// wave is set to Custom.
class CustomLfo : public visage::Frame {
public:
   explicit CustomLfo(unsigned color) : color_(color) { setIgnoresMouseEvents(true, false); }
   void setSteps(std::vector<float> steps, bool active) {
      if (steps != steps_ || active != active_) {
         steps_ = std::move(steps);
         active_ = active;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      if (steps_.empty())
         return;
      const ui::Theme &t = ui::defaultTheme();
      const float d = ui::designScale(*this), w = width(), h = height();
      const float top = 14 * d, ph = h - top, mid = top + ph * 0.5f, sw = w / steps_.size();
      c.setColor(t.dimText);
      c.text(active_ ? "CUSTOM" : "CUSTOM (OFF)", ui::font(kSmallText * d), visage::Font::kCenter, 0, 0, w, 13 * d);
      c.setColor(t.track);
      c.rectangle(0, mid, w, 1 * d);
      c.setColor(active_ ? color_ : dimTint(color_));
      for (size_t i = 0; i < steps_.size(); ++i) {
         const float v = std::clamp(steps_[i], -1.0f, 1.0f) * ph * 0.5f;
         c.rectangle(i * sw + 1 * d, std::min(mid, mid - v), sw - 2 * d, std::max(2 * d, std::abs(v)));
      }
   }

private:
   unsigned color_;
   std::vector<float> steps_;
   bool active_ = false;
};

// One of the synth's knobs that isn't on MIDI (fine tune, phones, master volume), drawn small and
// dim with its name beside it and no value. Its state is a 0-127 position, or -1 while unknown;
// setState() is the hook for when the plugin can read it. `ringed` adds the white ring the synth
// has round Master Volume.
class HwKnob : public visage::Frame {
public:
   explicit HwKnob(std::string label, unsigned labelColor = 0, bool ringed = false)
      : label_(std::move(label)), labelColor_(labelColor), ringed_(ringed) {}
   void setState(int state) {
      if (state != state_) {
         state_ = state;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = ui::designScale(*this), h = height();
      const float d = std::min(h - 6 * k, 36 * k), cx = d * 0.5f + 3 * k, cy = h * 0.5f;
      if (ringed_) {
         c.setColor(0xffd8dce0);
         c.ring(cx - d * 0.5f - 2 * k, cy - d * 0.5f - 2 * k, d + 4 * k, 1.5f * k);
      }
      c.setColor(t.track);
      c.ring(cx - d * 0.5f, cy - d * 0.5f, d, 3 * k);
      const float body = d * 0.72f;
      c.setColor(0xff23272d);
      c.circle(cx - body * 0.5f, cy - body * 0.5f, body);
      if (state_ >= 0) {
         const float a = (-0.75f + 1.5f * state_ / 127.0f) * 3.14159265f, px = std::sin(a), py = -std::cos(a);
         c.setColor(t.text);
         c.segment(cx + px * body * 0.12f, cy + py * body * 0.12f, cx + px * body * 0.4f, cy + py * body * 0.4f, 2 * k, true);
      }
      const float tx = d + 9 * k, tw = width() - tx;
      c.setColor(labelColor_ ? dimTint(labelColor_) : t.dimText);
      c.text(label_, ui::font(kNameText * k), visage::Font::kLeft, tx, cy - 18 * k, tw, 17 * k);
      c.setColor(t.dimText);
      c.text(state_ >= 0 ? std::to_string(state_) : std::string("NO MIDI"), ui::font(kSmallText * k), visage::Font::kLeft, tx,
             cy + 1 * k, tw, 15 * k);
   }

private:
   std::string label_;
   unsigned labelColor_;
   bool ringed_;
   int state_ = -1;
};

// The ring round the synth's big Master Cutoff knob: a silver bezel with ticks, drawn under the knob.
class Bezel : public visage::Frame {
public:
   Bezel() { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float k = ui::designScale(*this), w = width(), h = height(), d = std::min(w, h), cx = w * 0.5f, cy = h * 0.5f;
      c.setColor(visage::Brush::linear(visage::Color(0xffe4e8ec), visage::Color(0xff5f666e), visage::Point(cx - d * 0.5f, cy - d * 0.5f),
                                       visage::Point(cx + d * 0.5f, cy + d * 0.5f)));
      // A 270 degree ring like the knob's sweep, open at the bottom where the value sits.
      c.arc(cx - d * 0.5f, cy - d * 0.5f, d, 3 * k, -0.5f * 3.14159265f, 0.75f * 3.14159265f, true);
      c.setColor(0xff8a9199);
      for (int i = 0; i <= 10; ++i) {
         const float a = (-0.75f + 1.5f * i / 10.0f) * 3.14159265f, px = std::sin(a), py = -std::cos(a);
         const float r0 = d * 0.5f - 11 * k, r1 = d * 0.5f - 6 * k;
         c.segment(cx + px * r0, cy + py * r0, cx + px * r1, cy + py * r1, 1.5f * k, true);
      }
   }
};

// The matrix as it's printed on the synth: PRESET, SEQ and MOD above, the Mod Amount knob and the
// display; the 16 sources (A to P) down the left, the destinations along the top, and 256 buttons
// in 4 x 4 blocks with every fourth row in pink. Each cell's state is -1 while unknown (drawn dim),
// 0 for no routing, or a routing; the plugin can't read the routings from the synth's presets yet,
// so setCell() is the hook for when they're mapped. The display shows what the plugin knows.
class MatrixMap : public visage::Frame {
public:
   static constexpr const char *kSources[16] = {"ENV 1", "ENV 2", "ENV 3",      "ENV Follow", "LFO 1",    "LFO 2",
                                                "LFO 3", "Mod Wheel", "Kbd / Seq", "Aftertouch", "Velocity", "Seq Mod",
                                                "M1",    "M2",        "Exp 1 / M3", "Exp 2 / M4"};
   static constexpr const char *kDestinations[12][2] = {
      {"VCO 1", "Pitch"}, {"VCO 1", "Ultra"},    {"VCO 1", "PW"},       {"VCO 1", "Metal"},
      {"VCO 2", "Pitch"}, {"VCO 2", "Ultra"},    {"VCO 2", "PW"},       {"VCO 2", "Metal"},
      {"Steiner", "Cutoff"}, {"Ladder", "Cutoff"}, {"LFO 1", "AMT"}, {"VCA", ""}};

   explicit MatrixMap(unsigned labelColor = 0xffe6e8eb) : labelColor_(labelColor) {
      setIgnoresMouseEvents(true, false);
      cells_.fill(-1);
   }
   // The display's lines, up to four.
   void setDisplay(std::vector<std::string> lines) {
      if (lines != lines_) {
         lines_ = std::move(lines);
         redraw();
      }
   }
   void setCell(int source, int destination, int state) {
      int &cell = cells_[source * 16 + destination];
      if (cell != state) {
         cell = state;
         redraw();
      }
   }

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float k = ui::designScale(*this), w = width(), h = height();
      const auto small = ui::font(kSmallText * k);

      // PRESET, SEQ, MOD, the Mod Amount knob and the display.
      const char *modes[3] = {"PRESET", "SEQ", "MOD"};
      for (int i = 0; i < 3; ++i) {
         const float cx = 150 * k + i * 92 * k, cy = 58 * k, r = 30 * k;
         c.setColor(0xff4a5560);
         c.circle(cx - r, cy - r, 2 * r);
         c.setColor(0xff8da0ad);
         c.circle(cx - r + 3 * k, cy - r + 3 * k, 2 * r - 6 * k);
         c.setColor(0xff1a1d22);
         c.text(modes[i], small, visage::Font::kCenter, cx - r, cy - 8 * k, 2 * r, 16 * k);
      }
      c.setColor(t.dimText);
      c.text("Mod Amount", small, visage::Font::kLeft, 330 * k, 94 * k, 100 * k, 16 * k);
      const float kx = 440 * k, ky = 58 * k, kr = 34 * k;
      c.setColor(0xff0b0c0e);
      c.circle(kx - kr, ky - kr, 2 * kr);
      c.setColor(visage::Brush::linear(visage::Color(0xffd7dce0), visage::Color(0xff6c747c), visage::Point(kx - kr, ky - kr),
                                       visage::Point(kx + kr, ky + kr)));
      c.circle(kx - kr * 0.62f, ky - kr * 0.62f, 1.24f * kr);
      const float lx = 500 * k, lw = w - lx - 8 * k;
      c.setColor(0xff1b2a22);
      c.roundedRectangle(lx, 16 * k, lw, 86 * k, 3 * k);
      c.setColor(0xff9fe0b4);
      for (size_t i = 0; i < lines_.size() && i < 4; ++i)
         c.text(lines_[i], ui::font(12.5f * k), visage::Font::kLeft, lx + 10 * k, 22 * k + i * 19 * k, lw - 20 * k, 17 * k);

      // The grid. Blocks of four columns and rows sit a little apart, as on the synth.
      const float gx = 136 * k, gy = 172 * k, block = 8 * k;
      const float cw = (w - gx - 8 * k - 3 * block) / 16.0f;
      const float ch = (h - gy - 40 * k - 3 * block) / 16.0f;
      const auto colX = [&](int col) { return gx + col * cw + (col / 4) * block; };
      const auto rowY = [&](int row) { return gy + row * ch + (row / 4) * block; };
      // The column names at the smallest size that fits the narrowest of them.
      float widest = 1;
      for (const auto &d : kDestinations)
         widest = std::max({widest, textWidth(*this, d[0], kSmallText * k), textWidth(*this, d[1], kSmallText * k)});
      const auto head = ui::font(kSmallText * k * std::min(1.0f, (cw - 3 * k) / widest));
      for (int col = 0; col < 16; ++col) {
         const float x = colX(col);
         c.setColor(t.dimText);
         c.roundedRectangleBorder(x + 2 * k, 126 * k, cw - 4 * k, 34 * k, 2 * k, 1.0f);
         c.setColor(labelColor_);
         if (col < 12) {
            c.text(kDestinations[col][0], head, visage::Font::kCenter, x, 128 * k, cw, 15 * k);
            c.text(kDestinations[col][1], head, visage::Font::kCenter, x, 143 * k, cw, 15 * k);
         } else {
            c.text(std::to_string(col + 1), small, visage::Font::kCenter, x, 126 * k, cw, 34 * k);
         }
         c.setColor(t.dimText);
         const float nd = 22 * k, nx = x + cw * 0.5f - nd * 0.5f, ny = rowY(15) + ch + 8 * k;
         c.ring(nx, ny, nd, 1.2f * k);
         c.text(std::to_string(col + 1), small, visage::Font::kCenter, nx, ny, nd, nd);
      }
      for (int row = 0; row < 16; ++row) {
         const float y = rowY(row), mid = y + ch * 0.5f;
         c.setColor(t.dimText);
         c.ring(2 * k, mid - 11 * k, 22 * k, 1.2f * k);
         c.setColor(labelColor_);
         c.text(std::string(1, static_cast<char>('A' + row)), small, visage::Font::kCenter, 2 * k, mid - 11 * k, 22 * k, 22 * k);
         c.setColor(t.dimText);
         c.roundedRectangleBorder(28 * k, mid - 10 * k, 102 * k, 20 * k, 2 * k, 1.0f);
         c.setColor(labelColor_);
         c.text(kSources[row], small, visage::Font::kLeft, 33 * k, mid - 10 * k, 96 * k, 20 * k);
         const bool pink = row % 4 == 3;
         for (int col = 0; col < 16; ++col) {
            const float x = colX(col), m = std::min(cw, ch) * 0.12f;
            const int state = cells_[row * 16 + col];
            unsigned color = pink ? 0xff5b4248 : 0xff3a454f;
            if (state < 0)
               color = pink ? 0xff3a3034 : 0xff2a3138;
            else if (state > 0)
               color = 0xff6f8cff;
            c.setColor(color);
            c.roundedRectangle(x + m, y + m, cw - 2 * m, ch - 2 * m, 3 * k);
         }
      }
   }

private:
   unsigned labelColor_;
   std::vector<std::string> lines_;
   std::array<int, 256> cells_{};
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
