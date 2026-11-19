// J2D2's panel, on a 1100 x 680 grid: the C2 design from the mockups, in the house widgets.
//
//    header   the eye and name, VOICE | DRONES, the preset bar and the preset's line
//    VOICE    the spectrum (a waterfall scrolling right to left, each droid sound tagged as it
//             starts), the PERSONALITY pad with the moods placed around it and a dot for every
//             voice, the syllable mix, how velocity and pressure play the droid
//    DRONES   the five LAYERS, the mod sources (AMP, two ENVELOPES, two SHAPE LFOS), MOTION and
//             VOICING
//    both     the DROID <-> DRONE ribbon, SPACE, and MASTER with JSynth's glowing meters and its
//             stereo field
//
// Custom drawing glows the JSynth way: colours brighter than white (HDR) under a bloom post effect.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>
#include <jclap/ui/xy_pad.h>

#include <visage/graphics.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "params.h"
#include "presets.h"

namespace j2d2 {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1100.0f;
constexpr float kDesignHeight = 680.0f;
constexpr float kPi = 3.14159265f;

enum Page { kVoicePage, kDronePage };

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kLayerColors[kLayerCount] = {0xfff28c28, 0xff6fd8c8, 0xff9fc3ee, 0xffbb8fec, 0xfff5d76e};
constexpr unsigned kSyllableColors[kSyllableCount] = {0xff6fd8c8, 0xff9fc3ee, 0xffc9a0ff, 0xffffd36b,
                                                      0xffbb8fec, 0xfff28c28, 0xffff7a8a, 0xffa8e06b};
// ENV 1, ENV 2, LFO 1, LFO 2: JSynth's source colours.
constexpr unsigned kSourceColors[4] = {0xffffd36b, 0xffffa94d, 0xff5fd3b0, 0xffc9a0ff};
// The ribbon's six lines.
constexpr unsigned kRibbonColors[6] = {0xfff28c28, 0xfff5d76e, 0xff6fd8c8, 0xff9fc3ee, 0xffbb8fec, 0xffff7aa8};

// Steady colours (see jclap/ui/paint.h), with an HDR boost for the bloom to pick up.
visage::Color glow(unsigned argb, float alpha, float hdr = 1.0f) {
   return ui::steady(visage::Color(argb, hdr).withAlpha(std::clamp(alpha, 0.0f, 1.0f)));
}

std::string namesUpper(const char *name) {
   std::string out(name);
   for (char &ch : out)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
   return out;
}

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0, bool bipolar = false) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, bipolar,
           (kParams[id].flags & CLAP_PARAM_IS_STEPPED) != 0};
}

std::vector<std::string> namesOf(clap_id id) {
   int count = 0;
   const char *const *names = choiceNames(id, count);
   std::vector<std::string> out(names, names + count);
   for (std::string &s : out)
      for (char &ch : s)
         ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
   return out;
}

double seconds() {
   using namespace std::chrono;
   return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The time the panel draws at: real time, or a fixed moment for screenshots.
struct Clock {
   double fixed = -1.0;
   double now() const { return fixed >= 0.0 ? fixed : seconds(); }
};

// ---------------------------------------------------------------------------------------------
// The spectrum: a waterfall of the output, newest at the right, scrolling left, with a tag at the
// bottom for each droid sound as it starts.

class Spectrum : public visage::Frame {
public:
   static constexpr int kColumns = 220, kRows = 84, kFft = 2048;

   explicit Spectrum(EditorHost &host) : host_(host) {
      setIgnoresMouseEvents(true, false);
      for (auto &col : cols_)
         col.fill(0.0f);
      window_.resize(kFft);
      for (int i = 0; i < kFft; ++i)
         window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (kFft - 1));
   }

   // Takes the newest samples and pushes one column; notes any new droid sounds.
   void update() {
      std::vector<float> x(kFft);
      host_.readScope(x.data(), x.size());
      std::vector<std::complex<float>> a(kFft);
      for (int i = 0; i < kFft; ++i)
         a[i] = x[i] * window_[i];
      fft(a);
      std::array<float, kRows> col{};
      const float sr = 48000.0f;
      for (int r = 0; r < kRows; ++r) {
         // Rows on a log scale from 250 Hz to 12 kHz.
         const float lo = 250.0f * std::pow(48.0f, static_cast<float>(r) / kRows), hi = 250.0f * std::pow(48.0f, (r + 1.0f) / kRows);
         int b0 = static_cast<int>(lo / sr * kFft), b1 = std::max(b0 + 1, static_cast<int>(hi / sr * kFft));
         float m = 0.0f;
         for (int b = b0; b < b1 && b < kFft / 2; ++b)
            m = std::max(m, std::abs(a[b]));
         const float db = 20.0f * std::log10(m / (kFft * 0.25f) + 1e-7f);
         col[r] = std::clamp((db + 72.0f) / 66.0f, 0.0f, 1.0f);
      }
      head_ = (head_ + 1) % kColumns;
      cols_[head_] = col;
      ++count_;
      for (int i = 0; i < kMaxVoices; ++i) {
         const EditorHost::Voice v = host_.voice(i);
         const int ev = v.active ? v.event : 10;
         if (ev != lastEvent_[i] && ev < 8) {
            tags_.push_back({count_, ev, v.eventPitch});
            if (tags_.size() > 24)
               tags_.pop_front();
         }
         lastEvent_[i] = ev;
      }
      redraw();
   }

   // For screenshots: a made-up spectrum of a phrase, so the drawing has something in it.
   void demo(int page) {
      for (int c = 0; c < kColumns; ++c) {
         std::array<float, kRows> col{};
         const float t = static_cast<float>(c) / kColumns;
         const int seg = static_cast<int>(t * 6);
         const float u = t * 6 - seg;
         float pitch = 30;
         switch (page == kVoicePage ? seg % 5 : 5) {
         case 0: pitch = 22 + 22 * (u * u * (3 - 2 * u)); break;           // glide up
         case 1: pitch = 26 + 6 * std::floor(u * 5) - 3 * (static_cast<int>(u * 5) % 2); break; // steps
         case 2: pitch = 30 + 4 * std::sin(u * 40.0f); break;                // warble
         case 3: pitch = 40 - 22 * u; break;                                   // fall
         case 4: pitch = 28 + 14 * std::sin(u * 3.14f); break;                // bell
         default: pitch = 18; break;
         }
         const bool gap = u > 0.86f;
         for (int r = 0; r < kRows; ++r) {
            float v = 0.05f * (1.0f - static_cast<float>(r) / kRows);
            if (!gap)
               for (int h = 1; h <= 4; ++h) {
                  const float row = (pitch + 13.3f * std::log2(static_cast<float>(h))) * kRows / 64.0f;
                  v = std::max(v, (0.95f - 0.17f * h) * std::exp(-0.6f * (r - row) * (r - row)));
               }
            if (page == kDronePage)
               for (int k : {4, 11, 17, 21, 25, 28, 33})
                  v = std::max(v, (0.55f + 0.2f * std::sin(c * 0.05f + k)) * std::exp(-0.8f * (r - k * kRows / 64.0f) * (r - k * kRows / 64.0f)));
            col[r] = v;
         }
         head_ = (head_ + 1) % kColumns;
         cols_[head_] = col;
         ++count_;
         if (page == kVoicePage && u < 0.03f && (tags_.empty() || count_ - tags_.back().column > 8))
            tags_.push_back({count_, (seg * 3 + 1) % 8, pitch});
      }
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 236.0f;
      c.setColor(visage::Color(0xff0e1014));
      c.roundedRectangle(0, 0, w, h, 6 * s);
      const float left = 34 * s, top = 8 * s, bottom = h - 28 * s, cw = (w - left - 6 * s) / kColumns, rh = (bottom - top) / kRows;
      // Grid lines at 500 Hz, 1k, 2k, 4k, 8k.
      const auto yOf = [&](float hz) { return bottom - std::log(hz / 250.0f) / std::log(48.0f) * (bottom - top); };
      const char *labels[5] = {"500", "1k", "2k", "4k", "8k"};
      const float hzs[5] = {500, 1000, 2000, 4000, 8000};
      for (int i = 0; i < 5; ++i) {
         c.setColor(visage::Color(0x14ffffff));
         c.fill(left, yOf(hzs[i]), w - left - 6 * s, 1);
         c.setColor(visage::Color(0x88ffffff));
         c.text(labels[i], ui::font(9 * s), visage::Font::kRight, 0, yOf(hzs[i]) - 7 * s, left - 6 * s, 14 * s);
      }
      // The cells: dark slate through purple to orange and pale gold at the loudest.
      for (int k = 0; k < kColumns; ++k) {
         const auto &col = cols_[(head_ + 1 + k) % kColumns];
         const float x = left + k * cw;
         for (int r = 0; r < kRows; ++r) {
            const float v = col[r];
            if (v < 0.12f)
               continue;
            c.setColor(heat(v));
            c.fill(x, bottom - (r + 1) * rh, cw + 0.6f, rh + 0.6f);
         }
      }
      // NOW, at the right edge.
      c.setColor(visage::Color(0x99ffffff));
      c.text("NOW", ui::font(9 * s), visage::Font::kRight, w - 60 * s, top, 52 * s, 14 * s);
      // Tags, riding left with their sound.
      for (const Tag &t : tags_) {
         const long age = count_ - t.column;
         if (age >= kColumns)
            continue;
         const float x = left + (kColumns - 1 - age) * cw;
         const unsigned col = kSyllableColors[t.kind];
         c.setColor(glow(col, 1.0f, 1.3f));
         c.circle(x - 3 * s, h - 19 * s, 6 * s);
         c.setColor(visage::Color(0xccffffff));
         c.text(kSyllableNames[t.kind], ui::font(9 * s), visage::Font::kLeft, x + 6 * s, h - 24 * s, 70 * s, 12 * s);
      }
   }

private:
   struct Tag {
      long column;
      int kind;
      float pitch;
   };

   static visage::Color heat(float v) {
      // 0.12 .. 1: slate, purple, orange, pale gold.
      static const unsigned stops[4] = {0xff2a2440, 0xff7a4fd6, 0xfff28c28, 0xffffe2b0};
      const float x = std::clamp((v - 0.12f) / 0.88f, 0.0f, 1.0f) * 3.0f;
      const int i = std::min(2, static_cast<int>(x));
      const float f = x - i;
      const visage::Color a(stops[i]), b(stops[i + 1]);
      const auto mix = [f](float p, float q) { return p + (q - p) * f; };
      return ui::steady(visage::Color(mix(0.35f + 0.65f * v, 1.0f), mix(a.red(), b.red()), mix(a.green(), b.green()),
                                      mix(a.blue(), b.blue()), 1.0f));
   }

   static void fft(std::vector<std::complex<float>> &a) {
      const int n = static_cast<int>(a.size());
      for (int i = 1, j = 0; i < n; ++i) {
         int bit = n >> 1;
         for (; j & bit; bit >>= 1)
            j ^= bit;
         j ^= bit;
         if (i < j)
            std::swap(a[i], a[j]);
      }
      for (int len = 2; len <= n; len <<= 1) {
         const float ang = -2.0f * kPi / len;
         const std::complex<float> wl(std::cos(ang), std::sin(ang));
         for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
               const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
               a[i + j] = u + v;
               a[i + j + len / 2] = u - v;
               w *= wl;
            }
         }
      }
   }

   EditorHost &host_;
   std::array<std::array<float, kRows>, kColumns> cols_{};
   int head_ = 0;
   long count_ = 0;
   std::deque<Tag> tags_;
   int lastEvent_[kMaxVoices] = {10, 10, 10, 10, 10, 10, 10, 10};
   std::vector<float> window_;
};

// ---------------------------------------------------------------------------------------------
// The moods around the Personality pad, and a dot for each voice where its mood is now. Drawn over
// the XY pad; it lets the mouse through.

struct NamedMood {
   const char *name;
   float e, f;
   unsigned color;
};
constexpr NamedMood kMoods[7] = {{"EXCITED", 0.95f, 0.85f, 0xffff9f5a}, {"HAPPY", 0.35f, 0.75f, 0xfff5d76e},
                                 {"CURIOUS", 0.05f, 0.35f, 0xff6fd8c8}, {"WORRIED", 0.55f, -0.6f, 0xffbb8fec},
                                 {"GRUMPY", -0.05f, -0.85f, 0xffff7a8a}, {"SAD", -0.85f, -0.65f, 0xff9fc3ee},
                                 {"SLEEPY", -0.9f, 0.3f, 0xffa0a8b8}};

// The two moods nearest a point, as "34% happy · 31% curious".
std::string moodMix(float e, float f) {
   float w[7], sum = 0.0f;
   for (int i = 0; i < 7; ++i) {
      const float d = std::hypot(e - kMoods[i].e, f - kMoods[i].f);
      sum += (w[i] = 1.0f / (0.05f + d * d));
   }
   int a = 0, b = 1;
   for (int i = 0; i < 7; ++i)
      if (w[i] > w[a])
         a = i;
   b = a == 0 ? 1 : 0;
   for (int i = 0; i < 7; ++i)
      if (i != a && w[i] > w[b])
         b = i;
   const auto lower = [](const char *s) {
      std::string out(s);
      for (char &ch : out)
         ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      return out;
   };
   char buf[96];
   std::snprintf(buf, sizeof(buf), "now %d%% %s  ·  %d%% %s", static_cast<int>(100 * w[a] / sum), lower(kMoods[a].name).c_str(),
                 static_cast<int>(100 * w[b] / sum), lower(kMoods[b].name).c_str());
   return buf;
}

class MoodOverlay : public visage::Frame {
public:
   explicit MoodOverlay(EditorHost &host) : host_(host) { setIgnoresMouseEvents(true, false); }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 236.0f;
      const auto X = [&](float e) { return w * (0.5f + 0.44f * e); };
      const auto Y = [&](float f) { return h * (0.5f - 0.42f * f); };
      for (const NamedMood &m : kMoods) {
         c.setColor(glow(m.color, 0.85f, 1.0f));
         c.ring(X(m.e) - 4 * s, Y(m.f) - 4 * s, 8 * s, 1.4f * s);
         c.text(m.name, ui::font(9.5f * s), visage::Font::kCenter, X(m.e) - 40 * s, Y(m.f) - 20 * s, 80 * s, 12 * s);
      }
      c.setColor(visage::Color(0x77ffffff));
      c.text("BRIGHT", ui::font(9 * s), visage::Font::kLeft, 8 * s, 4 * s, 80 * s, 12 * s);
      c.text("GLUM", ui::font(9 * s), visage::Font::kLeft, 8 * s, h - 16 * s, 80 * s, 12 * s);
      // Each voice: a white line from the set point to where its mood is now, and a dot there.
      const float se = static_cast<float>(host_.paramValue(kEnergy)), sf = static_cast<float>(host_.paramValue(kFeeling));
      for (int i = 0; i < kMaxVoices; ++i) {
         const EditorHost::Voice v = demo_ ? demoVoice(i) : host_.voice(i);
         if (!v.active)
            continue;
         c.setColor(visage::Color(0x88ffffff));
         c.segment(X(se), Y(sf), X(v.energy), Y(v.feeling), 1.2f * s, true);
         c.setColor(glow(0xffffffff, 0.25f, 1.0f));
         c.circle(X(v.energy) - 8 * s, Y(v.feeling) - 8 * s, 16 * s);
         c.setColor(glow(0xffffffff, 1.0f, 1.4f));
         c.circle(X(v.energy) - 4 * s, Y(v.feeling) - 4 * s, 8 * s);
      }
   }

   bool demo_ = false;

private:
   EditorHost::Voice demoVoice(int i) const {
      EditorHost::Voice v;
      v.active = i < 2;
      v.energy = static_cast<float>(host_.paramValue(kEnergy)) + (i ? -0.18f : 0.2f);
      v.feeling = static_cast<float>(host_.paramValue(kFeeling)) + (i ? 0.12f : 0.22f);
      return v;
   }
   EditorHost &host_;
};

// ---------------------------------------------------------------------------------------------
// The DROID <-> DRONE ribbon: six coloured lines, jagged chatter at the left settling into calm
// held lines at the right, scrolling slowly. Even brightness end to end with a soft hump at the
// thumb, and a bloom on the lines within a fifth of the width either side of it. Drag anywhere.

class Ribbon : public visage::Frame {
public:
   Ribbon(EditorHost &host, const Clock &clock) : host_(host), clock_(clock) {
      bloom_.setBloomSize(10.0f);
      bloom_.setBloomIntensity(0.6f);
      setPostEffect(&bloom_);
   }

   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 72.0f;
      bloom_.setBloomSize(10.0f * s * (dpiScale() > 0.0f ? dpiScale() : 1.0f));
      const float m = static_cast<float>(host_.paramValue(kMorph));
      const float x0 = 200 * s, x1 = w - 200 * s, top = 22 * s, bottom = 56 * s;
      const double t = clock_.now();
      // The ends and the stages.
      c.setColor(visage::Color(m < 0.5f ? kOrange : 0xff8d939c));
      c.text("DROID", ui::font(17 * s), visage::Font::kCenter, 10 * s, 20 * s, 160 * s, 34 * s);
      c.setColor(visage::Color(m >= 0.5f ? kOrange : 0xff8d939c));
      c.text("DRONE", ui::font(17 * s), visage::Font::kCenter, w - 170 * s, 20 * s, 160 * s, 34 * s);
      const char *stages[4] = {"CHATTER", "SING", "HUM", "CHOIR"};
      for (int i = 0; i < 4; ++i) {
         const float u = i / 3.0f;
         c.setColor(visage::Color(std::fabs(u - m) < 0.17f ? 0xffffffff : 0xff8d939c));
         c.text(stages[i], ui::font(9.5f * s), visage::Font::kCenter, x0 + u * (x1 - x0) - 50 * s, 3 * s, 100 * s, 14 * s);
      }
      c.setColor(visage::Color(0xff0e1014));
      c.roundedRectangle(x0, top, x1 - x0, bottom - top, 4 * s);
      // The lines.
      constexpr int kSteps = 140;
      for (int line = 0; line < 6; ++line) {
         const float base = top + (bottom - top) * (0.2f + 0.12f * line);
         float px = 0, py = 0;
         for (int k = 0; k <= kSteps; ++k) {
            const float u = static_cast<float>(k) / kSteps;
            const float rough = std::pow(1.0f - u, 1.6f);
            const float ph = static_cast<float>(t) * 1.3f;
            const float n = std::sin(u * 61.0f + line * 1.9f + ph * 2.1f) * 0.55f + std::sin(u * 137.0f + line * 3.1f - ph * 3.3f) * 0.3f +
                            std::sin(u * 23.0f + line + ph * 0.7f) * 0.35f;
            const float y = base + n * rough * 8.0f * s + std::sin(u * 7.0f + line + ph * 0.3f) * 0.8f * s;
            const float x = x0 + u * (x1 - x0);
            if (k > 0) {
               const float uu = u - 0.5f / kSteps;
               const float near = std::exp(-0.5f * (uu - m) * (uu - m) / (0.06f * 0.06f));
               const float d = std::fabs(uu - m) / 0.2f;
               const float bloom = d >= 1.0f ? 0.0f : std::pow(std::cos(d * kPi / 2), 2.0f);
               c.setColor(glow(kRibbonColors[line], 0.62f * (0.8f + 0.2f * near), 1.0f + 0.45f * bloom));
               c.segment(px, py, x, y, (1.1f + 0.3f * near) * s, true);
            }
            px = x;
            py = y;
         }
      }
      // The seam and the thumb.
      const float tx = x0 + m * (x1 - x0);
      c.setColor(glow(0xffffffff, 0.95f, 1.1f));
      c.fill(tx - 1 * s, top - 6 * s, 2 * s, bottom - top + 12 * s);
      c.setColor(glow(kOrange, 1.0f, 1.0f));
      c.circle(tx - 8 * s, bottom + 2 * s, 16 * s);
      c.setColor(visage::Color(0xffffffff));
      c.circle(tx - 2.5f * s, bottom + 7.5f * s, 5 * s);
      // What this end does, opposite the thumb.
      char status[96];
      std::snprintf(status, sizeof(status), "%d%%  ·  %s", static_cast<int>(std::lround(m * 100)),
                    m < 0.02f   ? "each note speaks a phrase"
                    : m < 0.45f ? "phrases over a quiet drone"
                    : m < 0.98f ? "a drone, and pressure pops bleeps through"
                                : "every held note hums its own line");
      c.setColor(visage::Color(0x99ffffff));
      if (m < 0.5f)
         c.text(status, ui::font(9.5f * s), visage::Font::kRight, x1 - 400 * s, bottom + 4 * s, 400 * s, 14 * s);
      else
         c.text(status, ui::font(9.5f * s), visage::Font::kLeft, x0, bottom + 4 * s, 400 * s, 14 * s);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      dragging_ = true;
      host_.beginEdit(kMorph);
      setFrom(e.position.x);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (dragging_)
         setFrom(e.position.x);
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (dragging_)
         host_.endEdit(kMorph);
      dragging_ = false;
   }

private:
   void setFrom(float x) {
      const float s = height() / 72.0f, x0 = 200 * s, x1 = width() - 200 * s;
      host_.setParam(kMorph, std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f));
      redraw();
   }
   EditorHost &host_;
   const Clock &clock_;
   visage::BloomPostEffect bloom_;
   bool dragging_ = false;
};

// ---------------------------------------------------------------------------------------------
// MASTER: JSynth's LevelView meters (a halo in three rings that grows with the level, the bar
// brightening as it peaks, a tick where the master is set) and its StereoView field.

class MasterMeters : public visage::Frame {
public:
   explicit MasterMeters(EditorHost &host) : host_(host) {
      setIgnoresMouseEvents(true, false);
      bloom_.setBloomSize(8.0f);
      bloom_.setBloomIntensity(0.45f);
      setPostEffect(&bloom_);
   }
   void setLevels(float l, float r) {
      // Fast up, slow down.
      for (int i = 0; i < 2; ++i) {
         const float v = i ? r : l;
         level_[i] = v > level_[i] ? v : level_[i] * 0.88f;
      }
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 70.0f;
      const float top = 6 * s, bottom = h - 6 * s;
      const auto unit = [](double db) { return static_cast<float>((std::clamp(db, -60.0, 6.0) + 60.0) / 66.0); };
      const auto yOf = [&](double db) { return bottom - unit(db) * (bottom - top); };
      for (int db : {0, -12, -24, -36}) {
         c.setColor(visage::Color(db == 0 ? 0x44ffffff : 0x18ffffff));
         c.fill(0, yOf(db), w, 1);
      }
      const unsigned color = 0xffdfe6f5;
      for (int i = 0; i < 2; ++i) {
         const float bw = 12 * s, x = w * (0.28f + 0.44f * i) - bw / 2;
         c.setColor(visage::Color(0x16ffffff));
         c.roundedRectangle(x, top, bw, bottom - top, 3 * s);
         const double db = level_[i] > 1e-5f ? 20.0 * std::log10(level_[i]) : -90.0;
         const float u = unit(db), y = yOf(db);
         if (u > 0.0f) {
            for (int ring = 3; ring >= 1; --ring) {
               const float g = ring * (1.0f + 3.0f * u) * s;
               c.setColor(glow(color, 0.07f * u * (4 - ring), 1.0f));
               c.roundedRectangle(x - g, y - g, bw + 2 * g, bottom - y + g, 3 * s + g);
            }
            c.setColor(glow(color, 0.55f + 0.45f * u, 1.0f + 0.25f * u));
            c.roundedRectangle(x, y, bw, bottom - y, 3 * s);
            if (db > 0.0) {
               c.setColor(visage::Color(0xffff5a4a));
               c.roundedRectangle(x, y, bw, yOf(0.0) - y, 2 * s);
            }
         }
      }
      const float ty = yOf(host_.paramValue(kMasterLevel));
      c.setColor(visage::Color(0xeeffffff));
      c.fill(2 * s, ty - 1 * s, w - 4 * s, 2 * s);
   }

   float level_[2] = {0, 0};

private:
   EditorHost &host_;
   visage::BloomPostEffect bloom_;
};

class StereoField : public visage::Frame {
public:
   explicit StereoField(EditorHost &host, const int &page) : host_(host), page_(page) {
      setIgnoresMouseEvents(true, false);
      bloom_.setBloomSize(8.0f);
      bloom_.setBloomIntensity(0.45f);
      setPostEffect(&bloom_);
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 86.0f;
      const float fh = h - 14 * s, mid = fh / 2;
      c.setColor(visage::Color(0x40000000));
      c.roundedRectangle(0, 0, w, fh, 6 * s);
      c.setColor(visage::Color(0x22ffffff));
      c.fill(8 * s, mid, w - 16 * s, 1);
      c.fill(w / 2, 6 * s, 1, fh - 12 * s);
      const auto X = [&](float p) { return w / 2 + std::clamp(p, -1.0f, 1.0f) * (w / 2 - 12 * s); };
      const float width = static_cast<float>(host_.paramValue(kWidth)), spread = static_cast<float>(host_.paramValue(kSpread));
      const float half = 0.5f * std::min(width, 2.0f);
      c.setColor(visage::Color(0x55ffffff));
      for (float side : {-1.0f, 1.0f})
         c.fill(X(side * std::min(1.0f, half * 2.0f)) - 1 * s, 7 * s, 2 * s, fh - 14 * s);
      // The voices that are sounding, gathered.
      std::vector<EditorHost::Voice> voices;
      for (int i = 0; i < kMaxVoices; ++i) {
         EditorHost::Voice v = demo_ ? demoVoice(i) : host_.voice(i);
         if (v.active)
            voices.push_back(v);
      }
      // A lane for what is sounding: the droid's voices on the VOICE page, each drone layer on DRONES.
      struct Lane {
         unsigned color;
         float pan, spread;
      };
      std::vector<Lane> lanes;
      if (page_ == kDronePage) {
         static const float layerPan[kLayerCount] = {-0.1f, 0.1f, 0.4f, -0.3f, 0.0f};
         static const float layerSpread[kLayerCount] = {0.5f, 0.9f, 0.55f, 0.7f, 0.0f};
         for (int k = 0; k < kLayerCount; ++k)
            lanes.push_back({kLayerColors[k], layerPan[k] * spread, layerSpread[k] * spread});
      } else
         lanes.push_back({kOrange, 0.0f, 0.6f * spread});
      const int n = static_cast<int>(lanes.size());
      for (int li = 0; li < n; ++li) {
         const Lane &ln = lanes[li];
         const float my = n > 1 ? 7 * s + (fh - 14 * s) * (li + 0.5f) / n : mid;
         const float ph = n > 1 ? std::min(11 * s, (fh - 14 * s) / n - 2 * s) : 20 * s;
         const float l = X(ln.pan - ln.spread), r = X(ln.pan + ln.spread);
         c.setColor(glow(ln.color, 0.16f));
         c.roundedRectangle(std::min(l, r) - 4 * s, my - ph / 2, std::fabs(r - l) + 8 * s, ph, ph / 2);
         c.setColor(glow(ln.color, 0.9f, 1.2f));
         c.fill(X(ln.pan) - 1 * s, my - ph / 2 - 1 * s, 2 * s, ph + 2 * s);
         for (const EditorHost::Voice &v : voices) {
            const float e = std::clamp(page_ == kDronePage ? v.drone : std::max(v.droid * 2.0f, 0.3f), 0.2f, 1.0f);
            const float x = X(ln.pan + v.pan * (ln.spread > 0 ? 1.0f : 0.0f) * 1.25f * (ln.spread / std::max(0.01f, spread)));
            const float dot = n > 1 ? 0.7f : 1.0f;
            c.setColor(glow(ln.color, 0.25f * e, 1.0f));
            c.circle(x - 8 * s * dot, my - 8 * s * dot, 16 * s * dot);
            c.setColor(glow(ln.color, 1.0f, 1.6f));
            c.circle(x - 3.2f * s * dot, my - 3.2f * s * dot, 6.4f * s * dot);
         }
      }
      char label[48];
      std::snprintf(label, sizeof(label), "WIDTH %d%%  ·  SPREAD %d%%", static_cast<int>(std::lround(width * 100)),
                    static_cast<int>(std::lround(spread * 100)));
      c.setColor(visage::Color(0x88ffffff));
      c.text("L", ui::font(9 * s), visage::Font::kLeft, 2 * s, h - 12 * s, 20 * s, 12 * s);
      c.text("R", ui::font(9 * s), visage::Font::kRight, w - 22 * s, h - 12 * s, 20 * s, 12 * s);
      c.setColor(visage::Color(0xffe6e8eb));
      c.text(label, ui::font(9 * s), visage::Font::kCenter, 0, h - 12 * s, w, 12 * s);
   }
   bool demo_ = false;

private:
   EditorHost::Voice demoVoice(int i) const {
      EditorHost::Voice v;
      static const float pans[kMaxVoices] = {-0.4f, 0.3f, -0.15f, 0.45f, -0.3f, 0.2f, -0.05f, 0.35f};
      v.active = i < 4;
      v.pan = pans[i];
      v.droid = 0.5f;
      v.drone = 0.8f;
      return v;
   }
   EditorHost &host_;
   const int &page_;
   visage::BloomPostEffect bloom_;
};

// The voices in use, as JSynth's dot grid: lit while a voice plays.
class VoiceDots : public visage::Frame {
public:
   explicit VoiceDots(EditorHost &host) : host_(host) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 50.0f;
      const int limit = host_.paramValue(kPoly) >= 0.5 ? 1 : static_cast<int>(std::lround(host_.paramValue(kVoices)));
      int on = 0;
      for (int i = 0; i < kMaxVoices; ++i)
         on += demo_ ? i < 4 : host_.voice(i).active;
      for (int i = 0; i < kMaxVoices; ++i) {
         const float cx = 6 * s + (i % 4) * 17 * s, cy = 6 * s + (i / 4) * 16 * s, r = 5 * s;
         if (i >= limit) {
            c.setColor(visage::Color(0x14ffffff));
            c.circle(cx - r, cy - r, 2 * r);
            continue;
         }
         const bool lit = i < on;
         if (lit) {
            c.setColor(glow(kOrange, 0.25f));
            c.circle(cx - 2 * r, cy - 2 * r, 4 * r);
         }
         c.setColor(lit ? glow(kOrange, 1.0f, 1.5f) : glow(kOrange, 0.3f));
         c.circle(cx - r, cy - r, 2 * r);
      }
      char t[32];
      std::snprintf(t, sizeof(t), "%d OF %d VOICES", std::min(on, limit), limit);
      c.setColor(visage::Color(0x88ffffff));
      c.text(t, ui::font(9 * s), visage::Font::kLeft, 0, h - 13 * s, w, 12 * s);
      (void)w;
   }
   bool demo_ = false;

private:
   EditorHost &host_;
};

// ---------------------------------------------------------------------------------------------
// Small moving pictures: each drone layer's, the MOTION walks, an LFO's shape, the logo's eye.

class LayerView : public visage::Frame {
public:
   LayerView(EditorHost &host, const Clock &clock, int layer) : host_(host), clock_(clock), layer_(layer) {
      setIgnoresMouseEvents(true, false);
   }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 40.0f;
      c.setColor(visage::Color(0xff0e1014));
      c.roundedRectangle(0, 0, w, h, 4 * s);
      const unsigned col = kLayerColors[layer_];
      const float lvl = static_cast<float>(host_.paramValue(kLayer0 + layer_));
      const float t = static_cast<float>(clock_.now());
      const float a = 0.35f + 0.65f * lvl;
      switch (layer_) {
      case kLayerTone:
         for (int i = 0; i < 6; ++i) {
            const float y = h * (0.3f + 0.08f * i) + std::sin(t * 0.7f + i) * 0.6f * s;
            c.setColor(glow(col, a * (0.5f + 0.1f * i)));
            c.fill(5 * s, y, w - 10 * s, 1.2f * s);
         }
         break;
      case kLayerPartials: {
         const float atonal = static_cast<float>(host_.paramValue(kPartialsAtonal));
         for (int i = 0; i < 9; ++i) {
            const float y = h * (0.2f + 0.075f * i) + atonal * 2.5f * s * std::sin(i * 2.3f);
            const float x = (0.1f + 0.5f * (0.5f + 0.5f * std::sin(t * 0.3f + i * 1.7f))) * w;
            c.setColor(glow(col, a * (0.4f + 0.6f * (0.5f + 0.5f * std::sin(t * 0.5f + i)))));
            c.fill(x, y, w * 0.32f, 1.2f * s);
         }
         break;
      }
      case kLayerAir:
         for (int i = 0; i < 70; ++i) {
            const float fx = std::fmod(i * 0.618f + t * 0.03f * (1 + i % 3), 1.0f), fy = std::fmod(i * 0.377f, 1.0f);
            c.setColor(glow(col, a * 0.6f));
            c.fill(fx * w, h * (0.2f + 0.6f * fy), 1.2f * s, 1.2f * s);
         }
         c.setColor(glow(col, a * 0.5f));
         c.fill(5 * s, h * 0.55f, w - 10 * s, 1 * s);
         break;
      case kLayerMetal: {
         const float atonal = static_cast<float>(host_.paramValue(kMetalAtonal));
         for (int i = 0; i < 4; ++i) {
            const float y = h * (0.25f + 0.17f * i) + atonal * (i - 1.5f) * 1.6f * s;
            const float sw = 0.5f + 0.5f * std::sin(t * 0.4f + i * 2.1f);
            c.setColor(glow(col, a * (0.3f + 0.7f * sw)));
            c.segment(w * 0.3f, y + 1.5f * s, w * 0.7f, y - 1.5f * s, 1.4f * s, true);
         }
         break;
      }
      default: {
         float px = 0, py = 0;
         for (int k = 0; k <= 40; ++k) {
            const float u = k / 40.0f, x = 4 * s + u * (w - 8 * s);
            const float y = h * 0.5f + std::sin(u * 2 * kPi * 2 + t) * h * 0.28f * (0.6f + 0.4f * lvl);
            if (k) {
               c.setColor(glow(col, a));
               c.segment(px, py, x, y, 1.6f * s, true);
            }
            px = x;
            py = y;
         }
      }
      }
   }

private:
   EditorHost &host_;
   const Clock &clock_;
   int layer_;
};

class MotionView : public visage::Frame {
public:
   MotionView(EditorHost &host, const Clock &clock) : host_(host), clock_(clock) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 60.0f;
      c.setColor(visage::Color(0xff0e1014));
      c.roundedRectangle(0, 0, w, h, 4 * s);
      const float evolve = static_cast<float>(host_.paramValue(kEvolve)), drift = static_cast<float>(host_.paramValue(kDrift));
      const float t = static_cast<float>(clock_.now());
      for (int line = 0; line < 4; ++line) {
         float px = 0, py = 0;
         for (int k = 0; k <= 80; ++k) {
            const float u = k / 80.0f, x = 4 * s + u * (w - 8 * s);
            const float v = std::sin(u * (3 + 2 * evolve) * (1 + 0.3f * line) + line * 1.7f + t * 0.3f * evolve) * 0.6f +
                            std::sin(u * (7 + line) + t * 0.2f) * 0.25f * drift;
            const float y = h * 0.5f + v * h * 0.32f;
            if (k) {
               c.setColor(glow(kLayerColors[(line + 1) % kLayerCount], 0.75f));
               c.segment(px, py, x, y, 1.4f * s, true);
            }
            px = x;
            py = y;
         }
      }
   }

private:
   EditorHost &host_;
   const Clock &clock_;
};

class LfoView : public visage::Frame {
public:
   LfoView(EditorHost &host, int lfo) : host_(host), lfo_(lfo) { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), s = h / 50.0f;
      const unsigned col = kSourceColors[2 + lfo_];
      c.setColor(visage::Color(0xff0e1014));
      c.roundedRectangle(0, 0, w, h, 4 * s);
      const int shape = static_cast<int>(std::lround(host_.paramValue(lfo_ ? kLfo2Shape : kLfo1Shape)));
      const auto wave = [shape](float p) {
         p -= std::floor(p);
         switch (shape) {
         case kShapeTriangle: return 1.0f - 4.0f * std::fabs(p - 0.5f);
         case kShapeSaw: return 2.0f * p - 1.0f;
         case kShapeSquare: return p < 0.5f ? 1.0f : -1.0f;
         case kShapeSampleHold: return std::sin(std::floor(p * 4.0f) * 12.9898f) > 0.0f ? 0.7f : -0.5f;
         case kShapeSmooth: return 0.6f * std::sin(2.0f * kPi * p) + 0.4f * std::sin(2.0f * kPi * p * 0.37f + 1.0f);
         default: return std::sin(2.0f * kPi * p);
         }
      };
      float px = 0, py = 0;
      for (int k = 0; k <= 100; ++k) {
         const float u = k / 100.0f, x = 4 * s + u * (w - 8 * s), y = h * 0.5f - wave(u * 2.0f) * h * 0.36f;
         if (k) {
            c.setColor(glow(col, 0.9f));
            c.segment(px, py, x, y, 1.6f * s, true);
         }
         px = x;
         py = y;
      }
      const float v = demo_ ? 0.4f : host_.lfo(lfo_);
      const float y = h * 0.5f - v * h * 0.36f;
      c.setColor(glow(col, 0.3f));
      c.circle(w - 16 * s, y - 7 * s, 14 * s);
      c.setColor(glow(col, 1.0f, 1.4f));
      c.circle(w - 12 * s, y - 3 * s, 6 * s);
   }
   bool demo_ = false;

private:
   EditorHost &host_;
   int lfo_;
};

// The eye: an orange lens in a ring of the six mood colours.
class Logo : public visage::Frame {
public:
   Logo() { setIgnoresMouseEvents(true, false); }
   void draw(visage::Canvas &c) override {
      const float w = width(), h = height(), d = std::min(w, h);
      const float cx = d / 2, cy = h / 2;
      for (int i = 0; i < 6; ++i) {
         c.setColor(glow(kMoods[i].color, 0.95f));
         c.arc(cx - d / 2, cy - d / 2, d, d * 0.09f, -kPi / 2 + i * kPi / 3 + kPi / 6, kPi / 3.6f, true);
      }
      c.setColor(visage::Brush::radial(visage::Color(0xffffd9a0), visage::Color(0xffc0560c), visage::Point(cx - d * 0.08f, cy - d * 0.08f),
                                       d * 0.32f));
      c.circle(cx - d * 0.3f, cy - d * 0.3f, d * 0.6f);
      c.setColor(visage::Color(0xffffffff));
      c.circle(cx - d * 0.13f, cy - d * 0.17f, d * 0.1f);
   }
};

// A line of text that a lambda fills, redrawn on the panel's timer.
class LiveText : public visage::Frame {
public:
   LiveText(std::function<std::string()> fn, unsigned color, visage::Font::Justification j, float size)
       : fn_(std::move(fn)), color_(color), j_(j), size_(size) {
      setIgnoresMouseEvents(true, false);
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(visage::Color(color_));
      c.text(fn_(), ui::font(size_ * s), j_, 0, 0, width(), height());
   }

private:
   std::function<std::string()> fn_;
   unsigned color_;
   visage::Font::Justification j_;
   float size_;
};

// ---------------------------------------------------------------------------------------------

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      buildVoice();
      buildDrones();
      buildShared();
      tabs_->onChange = [this](int page) {
         page_ = page;
         modTabs_->showSelected();
         if (page == kVoicePage)
            for (visage::Frame *f : droneOnly_)
               f->setVisible(false);
         field_->redraw();
      };
      tabs_->select(kVoicePage);
      modTabs_->select(0);
      tabs_->onChange(kVoicePage);
      startTimer(33);
   }

   void showPage(int page, int modPage = 0) {
      tabs_->select(page);
      modTabs_->select(modPage);
      tabs_->onChange(page);
   }

   // A fixed, filled-in look for screenshots: a moment in time and made-up voices and spectrum.
   void demo(int page) {
      clock_.fixed = 12.0;
      spectrum_->demo(page);
      moods_->demo_ = field_->demo_ = dots_->demo_ = true;
      for (LfoView *l : lfos_)
         l->demo_ = true;
      meters_->level_[0] = 0.55f;
      meters_->level_[1] = 0.5f;
   }

   void timerCallback() override {
      float l = 0, r = 0;
      host_.outputPeaks(l, r);
      if (clock_.fixed < 0.0) {
         meters_->setLevels(l, r);
         if (page_ == kVoicePage)
            spectrum_->update();
      }
      for (visage::Frame *f : live_)
         if (f->isVisible())
            f->redraw();
   }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
      const float s = scale();
      canvas.setColor(ui::defaultTheme().headerBar);
      canvas.fill(0, 0, width(), 48 * s);
   }

private:
   template <typename T, typename... Args>
   T &onPage(Page page, ui::Rect bounds, Args &&...args) {
      return tabs_->addToPage(page, add<T>(bounds, std::forward<Args>(args)...));
   }

   ui::Knob &knob(Page page, ui::Rect r, clap_id id, const char *label, const char *tip, bool bipolar = false) {
      auto &k = onPage<ui::Knob>(page, r, rangeOf(id, 0.0, bipolar));
      k.setLabel(label);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      tips_.add(k, tip);
      return k;
   }
   ui::Knob &knobAll(ui::Rect r, clap_id id, const char *label, const char *tip) {
      auto &k = add<ui::Knob>(r, rangeOf(id));
      k.setLabel(label);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      tips_.add(k, tip);
      return k;
   }
   ui::Label &title(Page page, ui::Rect r, const char *t, unsigned color = 0xff8d939c) {
      return onPage<ui::Label>(page, r, t, color, visage::Font::kLeft, 0.8f);
   }

   void buildHeader() {
      add<Logo>({16, 8, 32, 32});
      add<ui::Label>({54, 10, 80, 28}, "J2D2", kOrange, visage::Font::kLeft, 0.95f);
      tips_.add(add<ui::HoverArea>({16, 8, 110, 32}), "J2D2 version " JCLAP_PLUGIN_VERSION);
      tabs_ = &add<ui::TabBar>({136, 12, 170, 26}, std::vector<std::string>{"VOICE", "DRONES"});
      presets_ = &add<ui::PresetBar>({318, 10, 380, 28}, factoryPresets(), kPresetFormat, host_);
      presets_->defaults = defaultValues();
      presets_->currentName = [this] { return host_.presetName(); };
      presets_->setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      add<LiveText>({710, 10, 288, 28}, [this] { return factoryBlurb(host_.presetName()); }, 0xff8d939c, visage::Font::kLeft, 8.5f);
      add<ui::UpdateNotice>({1000, 13, 84, 22});
   }

   // ---- VOICE ----
   void buildVoice() {
      onPage<ui::Section>(kVoicePage, {16, 58, 662, 262}, "SPECTRUM");
      spectrum_ = &onPage<Spectrum>(kVoicePage, {28, 82, 638, 228}, host_);
      onPage<LiveText>(kVoicePage, {200, 62, 460, 16}, [this] { return spectrumLine(); }, 0xff8d939c, visage::Font::kRight, 10.0f);

      onPage<ui::Section>(kVoicePage, {688, 58, 396, 262}, "PERSONALITY");
      auto &pad = onPage<ui::XYPad>(kVoicePage, {700, 84, 250, 210}, rangeOf(kEnergy, 0.01, true), rangeOf(kFeeling, 0.01, true));
      pad.x().label = "";
      pad.y().label = "";
      pad.x().valueText = [](double v) { return text(kEnergy, v); };
      pad.y().valueText = [](double v) { return text(kFeeling, v); };
      bindings_.bind(pad, kEnergy, kFeeling);
      tips_.add(pad, "The droid's mood: energy across (calm to excited), feeling up (glum to bright). Drag to set.");
      moods_ = &onPage<MoodOverlay>(kVoicePage, {700, 84, 250, 210}, host_);
      live_.push_back(moods_);
      onPage<LiveText>(kVoicePage, {700, 298, 250, 16},
                       [this] { return moodMix(static_cast<float>(host_.paramValue(kEnergy)), static_cast<float>(host_.paramValue(kFeeling))); },
                       0xffe6e8eb, visage::Font::kCenter, 10.0f);
      const float kx = 956, kw = 64;
      onPage<ui::Label>(kVoicePage, {kx, 66, 2 * kw, 14}, "WHAT MOVES IT", 0xff8d939c, visage::Font::kCenter, 0.8f);
      knob(kVoicePage, {kx, 84, kw, 78}, kPressToEnergy, "PRESS", "How far MPE pressure pushes a note's energy.", true);
      knob(kVoicePage, {kx + kw, 84, kw, 78}, kSlideToFeeling, "SLIDE", "How far MPE slide (CC 74) moves a note's feeling.", true);
      knob(kVoicePage, {kx, 176, kw, 78}, kVelToFeeling, "VELOCITY", "Harder notes are brighter (or glummer, below 0).", true);
      knob(kVoicePage, {kx + kw, 176, kw, 78}, kRandomMood, "RANDOM", "Each note's own nudge away from the set mood.");


      // Syllables.
      onPage<ui::Section>(kVoicePage, {16, 410, 340, 254}, "SYLLABLES");
      title(kVoicePage, {196, 416, 150, 14}, "how often each is spoken");
      for (int k = 0; k < kSyllableCount; ++k) {
         const clap_id id = kSyllable0 + k;
         auto &sl = onPage<ui::Slider>(kVoicePage, {26.0f + k * 40.5f, 446, 38, 190}, ui::Slider::Orientation::Vertical, rangeOf(id));
         sl.setLabel(kSyllableNames[k]);
         sl.setAccent(kSyllableColors[k]);
         sl.valueText = [id](double v) { return text(id, v); };
         bindings_.bind(sl, id);
      }
      onPage<ui::Label>(kVoicePage, {26, 640, 320, 14}, "0 never  ·  100% as the mood picks  ·  200% twice as often", 0xff6c727b);

      // Play: how the droid voice answers the keys.
      onPage<ui::Section>(kVoicePage, {366, 410, 258, 254}, "PLAY");
      knob(kVoicePage, {376, 440, 78, 92}, kVelResponse, "VELOCITY", "How far velocity opens the filter and drives the droid.");
      knob(kVoicePage, {456, 440, 78, 92}, kPressResponse, "PRESSURE", "How far pressure opens the filter and drives it while it talks.");
      knob(kVoicePage, {536, 440, 78, 92}, kTalk, "TALK", "How often a held note talks again. 0 speaks once per note.");
      knob(kVoicePage, {376, 552, 78, 92}, kPop, "POP", "Bleeps a second that full pressure on a held drone pops through.");
      knob(kVoicePage, {456, 552, 78, 92}, kRegister, "REGISTER", "Moves the droid's voice up or down.", true);
      knob(kVoicePage, {536, 552, 78, 92}, kKeyTrack, "KEY TRACK", "How far the key you play moves the droid's register.");
   }

   std::string spectrumLine() {
      int n = 0, low = 128;
      for (int i = 0; i < kMaxVoices; ++i) {
         const EditorHost::Voice v = host_.voice(i);
         if (v.active) {
            ++n;
            low = std::min(low, v.note);
         }
      }
      if (!n)
         return "play a note";
      static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s%d held  ·  %d voice%s", names[low % 12], low / 12 - 1, n, n == 1 ? "" : "s");
      return buf;
   }

   // ---- DRONES ----
   void buildDrones() {
      onPage<ui::Section>(kDronePage, {16, 58, 662, 262}, "LAYERS");
      title(kDronePage, {456, 64, 216, 14}, "each held note runs through all five");
      static const char *captions[kLayerCount] = {"detuned saws, sine body", "a cloud that blooms", "breath through resonators",
                                                  "bell modes", "lowest note only"};
      for (int k = 0; k < kLayerCount; ++k) {
         const float x = 26 + k * 130.0f;
         onPage<ui::Label>(kDronePage, {x, 86, 120, 16}, namesUpper(kLayerNames[k]), kLayerColors[k], visage::Font::kLeft, 0.85f);
         auto &lv = onPage<LayerView>(kDronePage, {x, 106, 120, 40}, host_, clock_, k);
         live_.push_back(&lv);
         auto &sl = onPage<ui::Slider>(kDronePage, {x, 154, 40, 140}, ui::Slider::Orientation::Vertical, rangeOf(kLayer0 + k));
         sl.setLabel("LEVEL");
         sl.setAccent(kLayerColors[k]);
         sl.valueText = [k](double v) { return text(kLayer0 + k, v); };
         bindings_.bind(sl, kLayer0 + k);
         if (k == kLayerPartials || k == kLayerMetal) {
            const clap_id id = k == kLayerPartials ? kPartialsAtonal : kMetalAtonal;
            auto &a = knob(kDronePage, {x + 46, 160, 74, 88}, id, "ATONAL",
                           k == kLayerPartials ? "Pure harmonics (0) to stretched, detuned and blue (1). Never chaos."
                                               : "A tuned bell (0) to a free bar's modes (1), metallic but still bell-like.");
            a.setAccent(kLayerColors[k]);
         }
         onPage<ui::Label>(kDronePage, {x, 298, 124, 14}, captions[k], 0xff6c727b, visage::Font::kLeft);
      }

      // Mod sources.
      onPage<ui::Section>(kDronePage, {688, 58, 396, 262}, "");
      modTabs_ = &onPage<ui::TabBar>(kDronePage, {700, 64, 372, 26}, std::vector<std::string>{"AMP", "ENVELOPES", "SHAPE LFOS"});
      const auto mod = [this](int sub, auto &frame) -> decltype(frame) {
         modTabs_->addToPage(sub, frame);
         droneOnly_.push_back(&frame);
         return frame;
      };
      // AMP.
      auto &env = mod(0, onPage<ui::EnvelopeEditor>(kDronePage, {700, 98, 372, 112}, rangeOf(kAttack), rangeOf(kDecay),
                                                     rangeOf(kSustain), rangeOf(kRelease)));
      for (clap_id id : {kAttack, kDecay, kSustain, kRelease})
         env.stage(static_cast<ui::EnvelopeEditor::Stage>(id - kAttack)).valueText = [id](double v) { return text(id, v); };
      bindings_.bind(env, kAttack, kDecay, kSustain, kRelease);
      const char *ampNames[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      for (int i = 0; i < 4; ++i)
         mod(0, knob(kDronePage, {706 + i * 92.0f, 216, 84, 96}, kAttack + i, ampNames[i], "The drone's amp envelope."));
      // ENVELOPES.
      for (int e = 0; e < 2; ++e) {
         const float x = 700 + e * 190.0f;
         const clap_id base = e ? kEnv2Attack : kEnv1Attack;
         mod(1, onPage<ui::Label>(kDronePage, {x, 98, 180, 16}, e ? "ENV 2  ·  SHORT" : "ENV 1  ·  SLOW", kSourceColors[e],
                                  visage::Font::kLeft, 0.85f));
         const char *n[4] = {"A", "D", "S", "R"};
         for (int i = 0; i < 4; ++i) {
            auto &k = mod(1, knob(kDronePage, {x + i * 45.0f, 118, 45, 66}, base + i, n[i], "This envelope's stages, per note."));
            k.setAccent(kSourceColors[e]);
         }
         mod(1, onPage<ui::Label>(kDronePage, {x, 192, 60, 14}, "TO", 0xff8d939c, visage::Font::kLeft));
         auto &dd = mod(1, onPage<ui::Dropdown>(kDronePage, {x, 208, 120, 26}, namesOf(base + 4)));
         bindings_.bind(dd, base + 4);
         auto &amt = mod(1, knob(kDronePage, {x + 120, 190, 60, 74}, base + 5, "AMOUNT", "How far, and which way.", true));
         amt.setAccent(kSourceColors[e]);
      }
      // SHAPE LFOS.
      for (int l = 0; l < 2; ++l) {
         const float x = 700 + l * 190.0f;
         const clap_id base = l ? kLfo2Rate : kLfo1Rate;
         mod(2, onPage<ui::Label>(kDronePage, {x, 98, 180, 16}, l ? "LFO 2  ·  WEATHER" : "LFO 1  ·  BREATH", kSourceColors[2 + l],
                                  visage::Font::kLeft, 0.85f));
         auto &view = mod(2, onPage<LfoView>(kDronePage, {x, 118, 180, 50}, host_, l));
         live_.push_back(&view);
         lfos_.push_back(&view);
         auto &rate = mod(2, knob(kDronePage, {x, 174, 60, 74}, base, "RATE", "How fast it runs, free."));
         rate.setAccent(kSourceColors[2 + l]);
         auto &shape = mod(2, onPage<ui::Dropdown>(kDronePage, {x + 64, 180, 116, 24}, namesOf(base + 1)));
         bindings_.bind(shape, base + 1);
         auto &dest = mod(2, onPage<ui::Dropdown>(kDronePage, {x + 64, 210, 116, 24}, namesOf(base + 2)));
         bindings_.bind(dest, base + 2);
         auto &amt = mod(2, knob(kDronePage, {x + 64, 238, 60, 74}, base + 3, "AMOUNT", "How far, and which way.", true));
         amt.setAccent(kSourceColors[2 + l]);
      }

      // Motion.
      onPage<ui::Section>(kDronePage, {16, 410, 340, 254}, "MOTION");
      title(kDronePage, {240, 416, 110, 14}, "nothing sits still");
      auto &mv = onPage<MotionView>(kDronePage, {28, 440, 316, 66}, host_, clock_);
      live_.push_back(&mv);
      knob(kDronePage, {26, 520, 80, 96}, kEvolve, "EVOLVE", "How fast every layer's motion moves.");
      knob(kDronePage, {108, 520, 80, 96}, kDrift, "DRIFT", "How far pitches wander, in cents.");
      knob(kDronePage, {190, 520, 80, 96}, kSpread, "SPREAD", "How wide the voices and partials spread across the field.");
      knob(kDronePage, {272, 520, 80, 96}, kBreath, "BREATH", "A slow swell in each note's level.");

      // Voicing.
      onPage<ui::Section>(kDronePage, {366, 410, 258, 254}, "VOICING");
      title(kDronePage, {500, 416, 116, 14}, "tuning and spread");
      bindings_.bind(onPage<ui::SegmentedSwitch>(kDronePage, {378, 444, 234, 28}, namesOf(kTuning)), kTuning);
      onPage<LiveText>(kDronePage, {378, 486, 234, 18}, [this] { return heldNotes(); }, 0xffe6e8eb, visage::Font::kLeft, 11.0f);
      onPage<ui::Label>(kDronePage, {378, 636, 234, 14}, "JUST tunes each note to the lowest held", 0xff6c727b, visage::Font::kLeft);
   }

   std::string heldNotes() {
      static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      static const double cents[12] = {0, 11.7, 3.9, 15.6, -13.7, -2.0, -9.8, 2.0, 13.7, -15.6, 17.6, -11.7};
      std::vector<int> notes;
      for (int i = 0; i < kMaxVoices; ++i) {
         const EditorHost::Voice v = host_.voice(i);
         if (v.active && v.note >= 0)
            notes.push_back(v.note);
      }
      const int n = static_cast<int>(notes.size());
      if (!n)
         return "hold a chord";
      std::sort(notes.begin(), notes.end());
      const bool just = host_.paramValue(kTuning) < 0.5;
      std::string out;
      for (int i = 0; i < n; ++i) {
         char buf[32];
         std::snprintf(buf, sizeof(buf), "%s%d  %+.0f ct    ", names[notes[i] % 12], notes[i] / 12 - 1,
                       just ? cents[(notes[i] - notes[0]) % 12] : 0.0);
         out += buf;
      }
      return out;
   }

   // ---- both pages ----
   void buildShared() {
      // The ribbon.
      add<ui::Section>({16, 328, 1068, 74});
      auto &ribbon = add<Ribbon>({20, 330, 1060, 72}, host_, clock_);
      live_.push_back(&ribbon);
      tips_.add(ribbon, "DROID <-> DRONE: from every note speaking a phrase, through phrases over a drone, to every held note humming. Drag.");

      // Space.
      add<ui::Section>({634, 410, 234, 254}, "SPACE");
      knobAll({642, 440, 74, 92}, kSpaceSize, "SIZE", "From a closet to a hall.");
      knobAll({716, 440, 74, 92}, kSpaceMix, "MIX", "How much of the room you hear.");
      knobAll({790, 440, 74, 92}, kSpaceDamp, "DAMP", "Darkens the room's tail.");
      knobAll({679, 552, 74, 92}, kWidth, "WIDTH", "The stereo width of everything.");

      // Master.
      add<ui::Section>({878, 410, 206, 254}, "MASTER");
      bindings_.bind(add<ui::SegmentedSwitch>({980, 418, 96, 22}, namesOf(kPoly)), kPoly);
      dots_ = &add<VoiceDots>({888, 446, 84, 50}, host_);
      live_.push_back(dots_);
      meters_ = &add<MasterMeters>({978, 446, 50, 70}, host_);
      auto &master = add<ui::Slider>({1032, 440, 40, 110}, ui::Slider::Orientation::Vertical, rangeOf(kMasterLevel, 0.5));
      master.setLabel("");
      master.valueText = [](double v) { return text(kMasterLevel, v); };
      bindings_.bind(master, kMasterLevel);
      auto &voices = add<ui::Slider>({886, 508, 88, 40}, ui::Slider::Orientation::Horizontal, rangeOf(kVoices, 1.0));
      voices.setLabel("VOICES");
      voices.valueText = [](double v) { return text(kVoices, v); };
      bindings_.bind(voices, kVoices);
      field_ = &add<StereoField>({888, 560, 186, 96}, host_, page_);
      live_.push_back(field_);
      live_.push_back(meters_);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   Clock clock_;
   int page_ = kVoicePage;
   ui::TabBar *tabs_ = nullptr, *modTabs_ = nullptr;
   ui::PresetBar *presets_ = nullptr;
   Spectrum *spectrum_ = nullptr;
   MoodOverlay *moods_ = nullptr;
   MasterMeters *meters_ = nullptr;
   StereoField *field_ = nullptr;
   VoiceDots *dots_ = nullptr;
   std::vector<LfoView *> lfos_;
   std::vector<visage::Frame *> live_, droneOnly_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

// page: 0 VOICE, 1 DRONES (AMP), 2 DRONES (ENVELOPES), 3 DRONES (SHAPE LFOS).
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath) {
   Panel panel(host);
   const int main = page == 0 ? kVoicePage : kDronePage;
   panel.showPage(main, page == 0 ? 0 : page - 1);
   panel.demo(main);
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace j2d2
