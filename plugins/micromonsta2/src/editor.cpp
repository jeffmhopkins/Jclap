// Visage panel for the MicroMonsta 2 controller, laid out on a fixed kDesignWidth x kDesignHeight
// grid and scaled to the window, which keeps its aspect ratio.
//
// Three bands, read top to bottom like the synth's signal: the audio path (sources, filter, voice,
// effects, out), the envelopes, and the modulation (sources feeding the mod matrix). Every control is
// one CC. The pictures in visuals.h are drawn from those CC values and the program's routing only.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cc_map.h"
#include "patches.h"
#include "visuals.h"

namespace mm2 {
namespace {

namespace ui = jclap::ui;
namespace vis = visuals;
using ui::Rect;

constexpr float kDesignWidth = 1640.0f;
constexpr float kDesignHeight = 1142.0f;
constexpr float kHeaderHeight = 64.0f;

// The grid: 16 point margins and gutters around three bands.
constexpr float kMargin = 16.0f, kGutter = 16.0f;
constexpr float kBand1Y = kHeaderHeight + kMargin, kBand1H = 340.0f;
constexpr float kBand2Y = kBand1Y + kBand1H + kGutter, kBand2H = 322.0f;
constexpr float kBand3Y = kBand2Y + kBand2H + kGutter, kBand3H = 352.0f;
static_assert(kBand3Y + kBand3H + kMargin == kDesignHeight, "the bands fill the panel");

// Band 1, left to right: sources, filter, voice, effects, out. Band 3: mod sources, mod matrix.
constexpr float kAudioW[] = {440, 392, 200, 424, 88};
constexpr float kModW[] = {500, 1092};
static_assert(440 + 392 + 200 + 424 + 88 + 4 * kGutter == kDesignWidth - 2 * kMargin, "band 1 fills the width");
static_assert(500 + 1092 + kGutter == kDesignWidth - 2 * kMargin, "band 3 fills the width");
constexpr float audioX(int column) {
   float x = kMargin;
   for (int i = 0; i < column; ++i)
      x += kAudioW[i] + kGutter;
   return x;
}

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;
constexpr unsigned kNoise = 0xffc9cdd3;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kDim = 0xff8d939c;
constexpr unsigned kHint = 0xff5d646e;
// Mod source colours: one per LFO and envelope, the performance controls, and grey for the rest.
constexpr unsigned kCyan = 0xff4fd6e0;
constexpr unsigned kYellow = 0xfff2c94c;
constexpr unsigned kPink = 0xffff6f91;
constexpr unsigned kWheel = 0xffd9a6f5;
constexpr unsigned kOscColors[] = {kOrange, kBlue, kPurple};
constexpr unsigned kLfoColors[] = {kCyan, kYellow, kPink};

// The colour a mod source is drawn in, by the synth's code for it ("lfo2*knb" is LFO 2's colour).
unsigned sourceColor(const ModRoute &r) {
   const std::string code = source(r).code;
   for (int i = 0; i < 3; ++i) {
      if (code.rfind("lfo" + std::to_string(i + 1), 0) == 0)
         return kLfoColors[i];
      if (code.rfind("env" + std::to_string(i + 1), 0) == 0)
         return kOscColors[i];
   }
   if (code == "mod knob")
      return kGreen;
   if (code == "modwheel")
      return kWheel;
   return kNoise;
}

// Where a destination is on the panel, and the knobs it moves (for their reach rings). Levels,
// envelope stages, volume and pan have no knob to ring.
struct Target {
   std::string where;
   std::vector<int> knobs;
};
Target targetOf(const ModRoute &r) {
   const int d = r.used() ? r.destination : 0;
   const auto n = [](int i) { return std::to_string(i); };
   if (d == 1)
      return {"OSC 1-3 \xC2\xB7 TUNE", {40, 43, 46}};
   if (d >= 2 && d <= 4)
      return {"OSC " + n(d - 1) + " \xC2\xB7 TUNE", {40 + 3 * (d - 2)}};
   if (d >= 5 && d <= 7)
      return {"OSC " + n(d - 4) + " \xC2\xB7 SHAPE", {39 + 3 * (d - 5)}};
   if (d >= 8 && d <= 10)
      return {"OSC " + n(d - 7) + " \xC2\xB7 LEVEL", {}};
   switch (d) {
   case 11: return {"NOISE \xC2\xB7 LEVEL", {}};
   case 12: return {"FILTER \xC2\xB7 CUTOFF", {74}};
   case 13: return {"FILTER \xC2\xB7 RESONANCE", {71}};
   case 14: return {"FILTER \xC2\xB7 FM", {90}};
   case 15: return {"VOICE \xC2\xB7 DRIVE", {91}};
   case 16: return {"VOICE \xC2\xB7 GLIDE", {5}};
   case 17: return {"VOICE VOLUME", {}};
   case 18: return {"VOICE PAN", {}};
   default: break;
   }
   // Envelope times come in groups of four: env 1-3, then all three.
   const char *stages[] = {"ATTACK", "DECAY", "RELEASE"};
   if (d >= 19 && d <= 30) {
      const int i = (d - 19) % 4;
      return {(i == 3 ? std::string("ENV 1-3") : "ENV " + n(i + 1)) + " \xC2\xB7 " + stages[(d - 19) / 4], {}};
   }
   if (d >= 31 && d <= 33)
      return {"LFO " + n(d - 30) + " \xC2\xB7 SPEED", {76 + d - 31}};
   if (d == 34)
      return {"LFO 1-3 \xC2\xB7 SPEED", {76, 77, 78}};
   return {};
}

// How far a slot's amount can push a knob, for its reach ring: an estimate, since the synth's own
// scaling isn't published.
double reachOf(double amount) { return std::fabs(amount - 64.0) * 0.7; }

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      buildSources();
      buildFilter();
      buildVoice();
      buildEffects();
      buildOut();
      buildEnvelopes();
      buildModSources();
      buildMatrix();
      syncChannel();
      syncRoutes();
      syncReadouts();
      startTimer(30);
   }

   void timerCallback() override {
      syncChannel();
      syncRoutes();
      syncReadouts();
      for (vis::Picture *p : pictures_)
         p->refresh();
      blinkLfos();
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
      canvas.text("Micro", ui::font(28 * s), visage::Font::kLeft, 64 * s, 10 * s, 100 * s, 44 * s);
      canvas.setColor(t.text);
      canvas.text("Monsta 2", ui::font(28 * s), visage::Font::kLeft, 143 * s, 10 * s, 160 * s, 44 * s);

      // Chevrons in the gutters show which way the signal flows.
      canvas.setColor(0xff3d444e);
      const auto chevron = [&](float x, float y) {
         canvas.segment((x - 3) * s, (y - 6) * s, (x + 3) * s, y * s, 2.2f * s, true);
         canvas.segment((x + 3) * s, y * s, (x - 3) * s, (y + 6) * s, 2.2f * s, true);
      };
      for (int c = 0; c < 4; ++c)
         chevron(audioX(c) + kAudioW[c] + kGutter * 0.5f, kBand1Y + kBand1H * 0.5f);
      chevron(kMargin + kModW[0] + kGutter * 0.5f, kBand3Y + kBand3H * 0.5f);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small waveform glyph.
      const float pts[][2] = {{18, 33}, {26, 33}, {30, 23}, {35, 44}, {40, 16}, {45, 48}, {49, 27}, {53, 33}, {58, 33}};
      canvas.setColor(kOrange);
      for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); ++i)
         canvas.segment(pts[i][0] * s, pts[i][1] * s, pts[i + 1][0] * s, pts[i + 1][1] * s, 2.5f * s, true);
   }

   // ---- building blocks ----

   const CcParam &param(int cc) const { return kCcParams[indexOfCc(cc)]; }
   ui::ValueRange rangeOf(int cc) const {
      const CcParam &p = param(cc);
      return ui::ValueRange{0.0, 127.0, defaultValue(p), 1.0, p.bipolar};
   }
   template <typename Control>
   void wire(Control &c, int cc, unsigned accent) {
      c.setAccent(accent);
      c.valueText = [](double v) { return std::to_string(toCcValue(v)); };
      bindings_.bind(c, static_cast<clap_id>(cc));
   }

   // A knob for a CC; label nullptr uses the CC's short label, "" shows none.
   ui::Knob &knob(int cc, unsigned accent, Rect cell, const char *label = nullptr) {
      auto &k = add<ui::Knob>(cell, rangeOf(cc));
      k.setLabel(label ? label : param(cc).label);
      k.setCaption("CC " + std::to_string(cc));
      wire(k, cc, accent);
      knobs_[cc] = &k;
      return k;
   }
   void knobRow(std::initializer_list<int> ccs, unsigned accent, Rect area) {
      const std::vector<Rect> cells = ui::row(area, static_cast<int>(ccs.size()));
      size_t i = 0;
      for (int cc : ccs)
         knob(cc, accent, cells[i++]);
   }
   ui::Slider &slider(int cc, unsigned accent, Rect r, const char *label = "") {
      auto &s = add<ui::Slider>(r, ui::Slider::Orientation::Horizontal, rangeOf(cc));
      s.setLabel(label);
      wire(s, cc, accent);
      return s;
   }
   // A slider with its value and CC underneath, for the levels.
   void level(int cc, unsigned accent, Rect r, const char *name = "") {
      auto &s = slider(cc, accent, r, name);
      if (!*name)
         s.valueText = [](double) { return std::string(); };
      readouts_.push_back({&label("", {r.x + 4, r.y + r.h + 1, 60, 13}, kWhite, visage::Font::kLeft, 0.85f), cc});
      caption(cc, {r.x + r.w - 64, r.y + r.h + 2, 60, 12}, visage::Font::kRight);
   }
   ui::Label &label(const std::string &text, Rect r, unsigned color = kWhite,
                    visage::Font::Justification j = visage::Font::kLeft, float fontScale = 0.78f) {
      return add<ui::Label>(r, text, color, j, fontScale);
   }
   // Small grey notes, such as a section's right-hand remark.
   ui::Label &hint(const std::string &text, Rect r, visage::Font::Justification j = visage::Font::kRight) {
      return label(text, r, kHint, j, 0.8f);
   }
   ui::Label &caption(int cc, Rect r, visage::Font::Justification j = visage::Font::kLeft) {
      return label("CC " + std::to_string(cc), r, kDim, j, 0.8f);
   }
   void section(const char *title, Rect r) { add<ui::Section>(r, title); }
   void divider(Rect r) { add<ui::Divider>(r); }
   template <typename T, typename... Args>
   T &picture(Rect r, Args &&...args) {
      auto &p = add<T>(r, ccValue(), std::forward<Args>(args)...);
      pictures_.push_back(&p);
      return p;
   }
   vis::CcValue ccValue() {
      return [this](int cc) { return host_.paramModulated(static_cast<clap_id>(cc)); };
   }

   // ---- header ----

   void buildHeader() {
      auto &bar = add<ui::PresetBar>({(kDesignWidth - 480) * 0.5f, 18, 480, 28}, factoryPresets(), kPresetFormat, host_);
      bar.setAccent(kOrange);
      bar.defaults = presetDefaults();
      bar.currentName = [this] { return host_.presetName(); };
      bar.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      bar.onLoad = [this](const ui::Preset &p) {
         if (const HardwarePatch *patch = findFactoryPatch(p.name))
            host_.loadProgram(*patch);
         else
            ui::applyPreset(p, host_, presetDefaults());
      };
      add<ui::UpdateNotice>({300, 20, 220, 24});

      place(chDown_, {1344, 18, 30, 28});
      place(channel_, {1378, 18, 116, 28});
      place(chUp_, {1498, 18, 30, 28});
      place(sendAll_, {1536, 18, 88, 28});
      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
   }

   // ---- band 1: the audio path ----

   void buildSources() {
      const float x = audioX(0), y = kBand1Y;
      section("SOURCES", {x, y, kAudioW[0], kBand1H});
      hint("MIX INTO FILTER", {x + 176, y + 10, 124, 12});
      const unsigned mixColors[4] = {kOrange, kBlue, kPurple, kNoise};
      picture<vis::MixBar>({x + 308, y + 11, 118, 10}, mixColors);

      // Column heads, then one row per oscillator: shape, tune, fine tune and its level.
      const char *heads[] = {"SHAPE", "TUNE", "FINE TUNE"};
      for (int c = 0; c < 3; ++c)
         label(heads[c], {x + 64 + 62 * c, y + 34, 62, 12}, kDim, visage::Font::kCenter, 0.8f);
      label("LEVEL", {x + 262, y + 34, 100, 12}, kDim, visage::Font::kLeft, 0.8f);
      for (int o = 0; o < 3; ++o) {
         const float ry = y + 44 + o * 80;
         label("OSC " + std::to_string(o + 1), {x + 14, ry + 16, 50, 20}, kOscColors[o]);
         for (int c = 0; c < 3; ++c)
            knob(39 + 3 * o + c, kOscColors[o], {x + 64 + 62 * c, ry - 14, 62, 92}, "");
         level(48 + o, kOscColors[o], {x + 262, ry + 8, 164, 30});
      }
      // Noise, and the detune that spreads all three oscillators.
      divider({x + 14, y + 280, kAudioW[0] - 28, 1});
      label("NOISE", {x + 14, y + 296, 56, 20}, kNoise);
      level(34, kOrange, {x + 84, y + 288, 160, 30}, "OSC DETUNE");
      level(51, kNoise, {x + 262, y + 288, 164, 30});
   }

   void buildFilter() {
      const float x = audioX(1), y = kBand1Y, w = kAudioW[1];
      section("FILTER", {x, y, w, kBand1H});
      hint("GUIDE \xC2\xB7 FILTER TYPE IS SET ON THE SYNTH", {x + 110, y + 10, w - 124, 12});
      picture<vis::FilterCurve>({x + 12, y + 34, w - 24, 170});
      knobRow({74, 71, 88, 89, 90}, kOrange, {x + 12, y + 210, w - 24, 100});
      hint("LINE: CUTOFF AND RESONANCE   \xC2\xB7   DASHED: ENV AMOUNT AT ITS PEAK", {x + 12, y + 318, w - 24, 12},
           visage::Font::kCenter);
   }

   void buildVoice() {
      const float x = audioX(2), y = kBand1Y, w = kAudioW[2];
      section("VOICE", {x, y, w, kBand1H});
      knobRow({5, 33}, kOrange, {x + 12, y + 30, w - 24, 96});
      knobRow({91, 35}, kOrange, {x + 12, y + 126, w - 24, 96});
      picture<vis::PanFan>({x + 12, y + 228, w - 24, 100});
   }

   void buildEffects() {
      const float x = audioX(3), y = kBand1Y, w = kAudioW[3];
      section("EFFECTS", {x, y, w, kBand1H});
      hint("PICTURES ARE GUIDES FROM THE KNOB VALUES", {x + 140, y + 10, w - 154, 12});
      const char *names[] = {"DELAY", "REVERB", "EQ"};
      const unsigned colors[] = {kBlue, kPurple, kGreen};
      for (int r = 0; r < 3; ++r) {
         const float ry = y + 32 + r * 100;
         if (r > 0)
            divider({x + 14, ry - 3, w - 28, 1});
         label(names[r], {x + 14, ry + 38, 56, 18}, colors[r], visage::Font::kLeft, 0.72f);
         const Rect pic{x + 72, ry + 12, 150, 72};
         if (r == 0)
            picture<vis::DelayTaps>(pic, kBlue);
         else if (r == 1)
            picture<vis::ReverbTail>(pic, kPurple);
         else
            picture<vis::EqTilt>(pic, kGreen);
      }
      for (int c = 0; c < 3; ++c) {
         knob(26 + c, kBlue, {x + 226 + 62 * c, y + 32, 62, 96});
         knob(29 + c, kPurple, {x + 226 + 62 * c, y + 132, 62, 96});
      }
      knob(25, kGreen, {x + 226, y + 232, 62, 96});
      hint("TILT EQ", {x + 296, y + 266, 110, 12}, visage::Font::kLeft);
      hint("64 IS FLAT", {x + 296, y + 282, 110, 12}, visage::Font::kLeft);
   }

   void buildOut() {
      const float x = audioX(4), y = kBand1Y, w = kAudioW[4];
      section("OUT", {x, y, w, kBand1H});
      auto &volume = add<ui::Slider>({x + 14, y + 32, w - 28, 270}, ui::Slider::Orientation::Vertical, rangeOf(7));
      volume.setLabel("VOLUME");
      wire(volume, 7, kWhite);
      tips_.add(volume, "Volume (CC 7). The synth doesn't save it in a program.");
      caption(7, {x + 4, y + 306, w - 8, 12}, visage::Font::kCenter);
      label("NOT SAVED", {x + 4, y + 320, w - 8, 11}, kHint, visage::Font::kCenter, 0.8f);
   }

   // ---- band 2: envelopes ----

   // Times sit on a square-root curve after a minimum width, so a zero attack or decay still leaves
   // room between the corners to grab each one.
   static constexpr float kMinStage = 0.14f;

   void buildEnvelopes() {
      const float x = kMargin, y = kBand2Y, w = kDesignWidth - 2 * kMargin;
      section("ENVELOPES", {x, y, w, kBand2H});
      hint("DRAG A CORNER, OR DRAG OR TYPE IN A BOX", {x + 800, y + 10, w - 814, 12});
      const int ccs[3][4] = {{73, 75, 79, 72}, {80, 81, 82, 83}, {84, 85, 86, 87}};
      const char *names[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      for (int e = 0; e < 3; ++e) {
         const float bx = x + 12 + e * 484;
         label("ENV " + std::to_string(e + 1), {bx + 2, y + 34, 80, 18}, kOscColors[e]);
         hint("ALSO A MOD SOURCE", {bx + 260, y + 38, 200, 11});
         auto &env = add<ui::EnvelopeEditor>({bx, y + 58, 460, 194}, rangeOf(ccs[e][0]), rangeOf(ccs[e][1]),
                                             rangeOf(ccs[e][2]), rangeOf(ccs[e][3]));
         env.setAccent(kOscColors[e]);
         env.setTimeMapping(
            [](double v) { return kMinStage + (1.0f - kMinStage) * std::sqrt(static_cast<float>(std::clamp(v, 0.0, 127.0) / 127.0)); },
            [](float p) {
               const double f = std::clamp((p - kMinStage) / (1.0f - kMinStage), 0.0f, 1.0f);
               return 127.0 * f * f;
            });
         for (int s = 0; s < 4; ++s) {
            env.stage(static_cast<ui::EnvelopeEditor::Stage>(s)).label = names[s];
            env.stage(static_cast<ui::EnvelopeEditor::Stage>(s)).valueText = [](double v) {
               return std::to_string(toCcValue(v));
            };
         }
         bindings_.bind(env, ccs[e][0], ccs[e][1], ccs[e][2], ccs[e][3]);
         for (int s = 0; s < 4; ++s) {
            const float cx = bx + s * 115.0f;
            label(names[s], {cx, y + 258, 115, 11}, kDim, visage::Font::kCenter, 0.85f);
            auto &box = add<ui::NumberBox>({cx + 23, y + 272, 69, 24}, rangeOf(ccs[e][s]));
            wire(box, ccs[e][s], kOscColors[e]);
            caption(ccs[e][s], {cx, y + 298, 115, 11}, visage::Font::kCenter);
         }
      }
      divider({x + 1460, y + 40, 1, 266});
      label("VELOCITY", {x + 1474, y + 34, 120, 16}, kDim, visage::Font::kCenter, 0.8f);
      knob(36, kOrange, {x + 1479, y + 56, 110, 120}, "VCA ENV");
      knob(37, kOrange, {x + 1479, y + 184, 110, 120}, "FILTER ENV");
   }

   // ---- band 3: modulation ----

   void buildModSources() {
      const float x = kMargin, y = kBand3Y, w = kModW[0];
      section("MOD SOURCES", {x, y, w, kBand3H});
      hint("LIGHTS BLINK AT ROUGHLY EACH SPEED", {x + 200, y + 10, w - 214, 12});
      for (int i = 0; i < 3; ++i) {
         const float tx = x + 12 + i * 162, ty = y + 34, tw = 152;
         add<vis::Tile>({tx, ty, tw, 170});
         label("LFO " + std::to_string(i + 1), {tx + 10, ty + 8, 60, 16}, kLfoColors[i]);
         hint("RATE", {tx + tw - 74, ty + 10, 44, 12});
         auto &led = add<ui::ActivityLed>({tx + tw - 26, ty + 8, 16, 16});
         led.setColor(kLfoColors[i]);
         led.fadeMs = 0;
         led.setLevel(1.0f);
         leds_[i] = &led;
         knob(76 + i, kLfoColors[i], {tx + 26, ty + 26, 100, 116}, "SPEED");
         feeds_[i] = &label("", {tx + 6, ty + 150, tw - 12, 12}, kLfoColors[i], visage::Font::kCenter, 0.8f);
      }
      hint("PERFORMANCE \xC2\xB7 NOT SAVED IN THE PROGRAM \xC2\xB7 ENV 1-3 CAN FEED THE MATRIX TOO", {x + 12, y + 214, w - 24, 12},
           visage::Font::kLeft);

      const float py = y + 232;
      add<vis::Tile>({x + 12, py, 233, 108});
      auto &wheel = add<vis::WheelStrip>({x + 26, py + 8, 30, 92}, rangeOf(1));
      wire(wheel, 1, kWheel);
      label("MOD WHEEL", {x + 70, py + 16, 170, 16}, kWheel);
      wheelValue_ = &label("", {x + 70, py + 36, 170, 20}, kWhite, visage::Font::kLeft, 0.8f);
      caption(1, {x + 70, py + 58, 170, 11});
      feeds_[3] = &label("", {x + 70, py + 80, 166, 12}, kWheel, visage::Font::kLeft, 0.8f);

      add<vis::Tile>({x + 255, py, 233, 108});
      knob(70, kGreen, {x + 261, py + 2, 90, 104}, "");
      label("MOD KNOB", {x + 356, py + 16, 128, 16}, kGreen);
      feeds_[4] = &label("", {x + 356, py + 80, 128, 12}, kGreen, visage::Font::kLeft, 0.8f);
   }

   void buildMatrix() {
      const float x = kMargin + kModW[0] + kGutter, y = kBand3Y, w = kModW[1];
      section("MOD MATRIX", {x, y, w, kBand3H});
      hint("SLOTS SHOW THE LOADED PROGRAM'S ROUTING", {x + 600, y + 10, w - 614, 12});
      // Columns: slot, source, arrow, destination, where it is on the panel, amount, value, CC.
      constexpr float kSlot = 12, kSource = 48, kArrow = 152, kDest = 180, kWhere = 320, kAmount = 500,
                      kAmountW = 470, kValue = 980, kCc = 1032;
      const auto head = [&](const char *t, float cx, float cw, visage::Font::Justification j = visage::Font::kLeft) {
         label(t, {x + cx, y + 34, cw, 12}, kDim, j, 0.8f);
      };
      head("SLOT", kSlot, 36);
      head("SOURCE", kSource, 100);
      head("DESTINATION", kDest, 136);
      head("ON THE PANEL", kWhere, 170);
      head("\xE2\x88\x92 REVERSED", kAmount, 120);
      head("NONE", kAmount, kAmountW, visage::Font::kCenter);
      head("MORE +", kAmount, kAmountW, visage::Font::kRight);
      head("AMOUNT", kValue - 20, 64, visage::Font::kRight);
      head("CC", kCc, 48);
      for (int s = 0; s < kModSlots; ++s) {
         const float ry = y + 52 + s * 24.0f;
         Row &row = rows_[s];
         label(std::to_string(s + 1), {x + kSlot + 6, ry + 5, 30, 14}, kDim);
         row.chip = &add<vis::Chip>({x + kSource, ry + 3, 100, 18});
         row.arrow = &add<vis::Arrow>({x + kArrow, ry + 6, 18, 12});
         row.destination = &label("", {x + kDest, ry + 5, 136, 14});
         row.where = &label("", {x + kWhere, ry + 5, 170, 14}, kDim);
         row.amount = &slider(52 + s, kNoise, {x + kAmount, ry - 4, kAmountW, 26});
         row.amount->valueText = [](double) { return std::string(); };
         row.value = &label("", {x + kValue, ry + 5, 44, 14}, kWhite, visage::Font::kRight);
         caption(52 + s, {x + kCc, ry + 6, 48, 12});
         tips_.add(*row.amount, modSentence(s, false, {}));
      }
      divider({x + 12, y + 296, w - 24, 1});
      label("LAG 1", {x + kSlot + 6, y + 309, 130, 14});
      hint("SMOOTHS THE LAG SOURCE", {x + kDest, y + 310, 300, 12}, visage::Font::kLeft);
      auto &lag = slider(62, kOrange, {x + kAmount, y + 300, kAmountW, 26});
      lag.valueText = [](double) { return std::string(); };
      lagValue_ = &label("", {x + kValue, y + 309, 44, 14}, kWhite, visage::Font::kRight);
      caption(62, {x + kCc, y + 310, 48, 12});
      hint("ROUTING COMES FROM THE LOADED PROGRAM; ONLY THE AMOUNT IS A CC. A FAINT RING ON A KNOB SHOWS ROUGHLY "
           "HOW FAR ITS SLOT CAN PUSH IT. HOVER A ROW FOR THE SENTENCE.",
           {x + 12, y + 334, w - 24, 12}, visage::Font::kLeft);
   }

   // ---- keeping it current ----

   void syncChannel() { channel_.setText("MIDI CH " + std::to_string(host_.channel())); }

   // The matrix rows follow the loaded program's routing, in plain English.
   void syncRoutes() {
      const int program = host_.program();
      bool changed = program != shownProgram_;
      ModRoute routes[kModSlots];
      for (int s = 0; s < kModSlots; ++s) {
         routes[s] = host_.route(s);
         changed = changed || !(routes[s] == shownRoutes_[s]);
      }
      if (!changed)
         return;
      shownProgram_ = program;
      for (int s = 0; s < kModSlots; ++s) {
         shownRoutes_[s] = routes[s];
         Row &row = rows_[s];
         const bool used = program > 0 && routes[s].used();
         const unsigned color = used ? sourceColor(routes[s]) : kDim;
         row.chip->set(used ? source(routes[s]).label : "", color);
         row.arrow->setColor(color);
         row.amount->setAccent(used ? color : 0xff4b515a);
         if (program == 0) {
            row.destination->setText(s == 0 ? "PICK A PROGRAM" : "");
            row.destination->setColor(kDim);
            row.where->setText(s == 0 ? "TO SEE ITS ROUTING" : "");
         }
         else if (!used) {
            row.destination->setText("OFF");
            row.destination->setColor(kDim);
            row.where->setText("");
         }
         else {
            const ModName &dest = destination(routes[s]);
            std::string text = dest.knob ? dest.knob : dest.label;
            for (char &c : text)
               c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            row.destination->setText(text);
            row.destination->setColor(kWhite);
            row.where->setText(targetOf(routes[s]).where);
         }
         tips_.setText(*row.amount, modSentence(s, program > 0, routes[s]));
      }
      // Which slots each LFO and performance control feeds.
      const auto feeds = [&](auto matches) {
         std::string list;
         for (int s = 0; s < kModSlots; ++s)
            if (program > 0 && routes[s].used() && matches(std::string(source(routes[s]).code)))
               list += (list.empty() ? "" : " \xC2\xB7 ") + std::to_string(s + 1);
         return list.empty() ? std::string("NOT ROUTED") : "FEEDS SLOT " + list;
      };
      for (int i = 0; i < 3; ++i) {
         const std::string lfo = "lfo" + std::to_string(i + 1);
         feeds_[i]->setText(feeds([&](const std::string &c) { return c.rfind(lfo, 0) == 0; }));
      }
      feeds_[3]->setText(feeds([](const std::string &c) { return c == "modwheel" || c.find("whl") != std::string::npos; }));
      feeds_[4]->setText(feeds([](const std::string &c) {
         return c == "mod knob" || c.find("knb") != std::string::npos;
      }));
   }

   // Numbers next to the sliders, and each targeted knob's reach ring.
   void syncReadouts() {
      const auto value = [this](int cc) { return host_.paramModulated(static_cast<clap_id>(cc)); };
      for (int s = 0; s < kModSlots; ++s) {
         const int v = toCcValue(value(52 + s)) - 64;
         rows_[s].value->setText((v > 0 ? "+" : "") + std::to_string(v));
         rows_[s].value->setColor(shownProgram_ > 0 && shownRoutes_[s].used() ? kWhite : kDim);
      }
      lagValue_->setText(std::to_string(toCcValue(value(62))));
      for (auto [readout, cc] : readouts_)
         readout->setText(std::to_string(toCcValue(value(cc))));
      wheelValue_->setText(std::to_string(toCcValue(value(1))));

      double reach[128] = {};
      unsigned color[128] = {};
      if (shownProgram_ > 0)
         for (int s = 0; s < kModSlots; ++s) {
            if (!shownRoutes_[s].used())
               continue;
            const double r = reachOf(value(52 + s));
            for (int cc : targetOf(shownRoutes_[s]).knobs)
               if (r > reach[cc]) {
                  reach[cc] = r;
                  color[cc] = sourceColor(shownRoutes_[s]);
               }
         }
      for (int cc = 0; cc < 128; ++cc)
         if (knobs_[cc])
            knobs_[cc]->setReach(reach[cc], color[cc]);
   }

   // The rate lights blink at a guess of each LFO's speed (the synth doesn't publish its rates).
   void blinkLfos() {
      for (int i = 0; i < 3; ++i) {
         const double speed = host_.paramModulated(static_cast<clap_id>(76 + i)) / 127.0;
         const double hz = 0.2 * std::pow(2.0, 7.0 * speed);
         lfoPhase_[i] = std::fmod(lfoPhase_[i] + hz * 0.03, 1.0);
         leds_[i]->setLevel(lfoPhase_[i] < 0.5 ? 1.0f : 0.15f);
      }
   }

   struct Row {
      vis::Chip *chip = nullptr;
      vis::Arrow *arrow = nullptr;
      ui::Label *destination = nullptr, *where = nullptr, *value = nullptr;
      ui::Slider *amount = nullptr;
   };

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::Knob *knobs_[128] = {};
   std::vector<vis::Picture *> pictures_;
   Row rows_[kModSlots];
   std::vector<std::pair<ui::Label *, int>> readouts_;
   ui::Label *lagValue_ = nullptr, *wheelValue_ = nullptr;
   ui::Label *feeds_[5] = {}; // LFO 1-3, mod wheel, mod knob
   ui::ActivityLed *leds_[3] = {};
   double lfoPhase_[3] = {};
   int shownProgram_ = -1;
   ModRoute shownRoutes_[kModSlots];
   ui::Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath) {
   Panel panel(host);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace mm2
