// Visage panel for the MatrixBrute controller, laid out on a fixed kDesignWidth x kDesignHeight grid
// and scaled to the window, which keeps its aspect ratio.
//
// It follows the synth's own panel: the synthesis sections on the left in three rows (oscillators,
// then mixer and filters, then performance, LFOs and envelopes), and on the right the analog
// effects above the matrix, with the sequencer beside it. Every control is one CC. Knobs the plugin
// hasn't heard from the synth or set itself are drawn dim, since their value is a guess.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/presets.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cc_map.h"
#include "visuals.h"

namespace mb {
namespace {

namespace ui = jclap::ui;
namespace vis = visuals;
using ui::Rect;

constexpr float kDesignWidth = 1800.0f;
constexpr float kDesignHeight = 1100.0f;
constexpr float kHeaderHeight = 64.0f;

// The grid: 16 point margins and gutters. The synthesis sections fill the left, in three rows;
// the effects, matrix and sequencer fill the right.
constexpr float kMargin = 16.0f, kGutter = 16.0f;
constexpr float kRightW = 620.0f;
constexpr float kLeftW = kDesignWidth - 2 * kMargin - kGutter - kRightW;
constexpr float kRightX = kMargin + kLeftW + kGutter;
constexpr float kRow1Y = kHeaderHeight + kMargin, kRow1H = 300.0f;
constexpr float kRow2Y = kRow1Y + kRow1H + kGutter, kRow2H = 300.0f;
constexpr float kRow3Y = kRow2Y + kRow2H + kGutter, kRow3H = 372.0f;
static_assert(kRow3Y + kRow3H + kMargin == kDesignHeight, "the rows fill the panel");

constexpr float kRow1W[] = {392, 392, 116, 184};    // VCO 1, VCO 2, VCO 3 + noise, audio mod
constexpr float kRow2W[] = {284, 340, 120, 340};    // mixer, Steiner, master cutoff, ladder
constexpr float kRow3W[] = {204, 130, 130, 196, 196, 196}; // performance, LFO 1-2, ENV 1-3
static_assert(392 + 392 + 116 + 184 + 3 * kGutter == kLeftW, "row 1 fills the left");
static_assert(284 + 340 + 120 + 340 + 3 * kGutter == kLeftW, "row 2 fills the left");
static_assert(204 + 130 + 130 + 3 * 196 + 5 * kGutter == kLeftW, "row 3 fills the left");
template <size_t N>
constexpr float columnX(const float (&widths)[N], size_t column) {
   float x = kMargin;
   for (size_t i = 0; i < column; ++i)
      x += widths[i] + kGutter;
   return x;
}

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;
constexpr unsigned kRed = 0xffff5b5b;
constexpr unsigned kNoise = 0xffc9cdd3;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kDim = 0xff8d939c;
constexpr unsigned kHint = 0xff5d646e;
constexpr unsigned kUnknown = 0xff4b515a;
constexpr unsigned kCyan = 0xff4fd6e0;
constexpr unsigned kYellow = 0xfff2c94c;
constexpr unsigned kPink = 0xffff6fb5;
constexpr unsigned kMatrixBlue = 0xff5aa9ff;
constexpr unsigned kWheel = 0xffd9a6f5;
// The filters' own colours: the synth lights the Steiner routing red.
constexpr unsigned kSteiner = kRed;
constexpr unsigned kLadder = kBlue;
constexpr unsigned kEnvColors[] = {kOrange, kGreen, kPurple};

// User presets are the knobs' values; there's no factory bank until the synth's own presets can be read.
const ui::PresetFormat kPresetFormat{"MatrixBrute CC", 1, ".mbpreset", 128};
std::vector<double> presetDefaults() {
   std::vector<double> d(kPresetFormat.paramCount, 0.0);
   for (const CcParam &p : kCcParams)
      d[p.cc] = defaultValue(p);
   return d;
}
const std::vector<ui::Preset> &factoryPresets() {
   static const std::vector<ui::Preset> none;
   return none;
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      buildVco(0, 1, kOrange, {9, 10, 12, 13, 66, 65, 68, 69, 70, 67});
      buildVco(1, 2, kBlue, {14, 15, 16, 17, 72, 71, 74, 75, 76, 73});
      buildVco3();
      buildAudioMod();
      buildMixer();
      buildFilter(1, "STEINER-PARKER \xC2\xB7 VCF 1", kSteiner, {23, 83, 82, 81, 24, 7});
      buildMasterCutoff();
      buildFilter(3, "LADDER \xC2\xB7 VCF 2", kLadder, {25, 87, 86, 85, 26, 8});
      buildPerformance();
      buildLfo(0, 91, 90, kCyan);
      buildLfo(1, 93, 92, kYellow);
      buildEnvelope(0, {-1, 102, 103, 28, 104}, 94, "VELO > VCF");
      buildEnvelope(1, {-1, 105, 106, 29, 107}, 95, "VELO > VCA");
      buildEnvelope(2, {108, 109, 110, 30, 111}, -1, "");
      buildFx();
      buildMatrix();
      buildSequencer();
      sync();
      startTimer(30);
   }

   void timerCallback() override {
      sync();
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
      canvas.text("Matrix", ui::font(28 * s), visage::Font::kLeft, 64 * s, 10 * s, 100 * s, 44 * s);
      canvas.setColor(t.text);
      canvas.text("Brute", ui::font(28 * s), visage::Font::kLeft, 146 * s, 10 * s, 100 * s, 44 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small 4 x 4 grid of matrix buttons with a diagonal lit.
      for (int r = 0; r < 4; ++r)
         for (int c = 0; c < 4; ++c) {
            canvas.setColor(r == c ? kOrange : 0xff3d444e);
            canvas.roundedRectangle((20 + c * 9) * s, (16 + r * 9) * s, 7 * s, 7 * s, 1.5f * s);
         }
   }

   // ---- building blocks ----

   const CcParam &param(int cc) const { return kCcParams[indexOfCc(cc)]; }
   ui::ValueRange rangeOf(int cc) const {
      const CcParam &p = param(cc);
      return ui::ValueRange{0.0, 127.0, defaultValue(p), 1.0, p.bipolar};
   }
   // The number under a control: whole CC values, two decimals for a 14-bit one while fine bytes
   // are on, and a dash for one the plugin doesn't know.
   std::string valueText(int cc, double v) const {
      if (!host_.known(cc))
         return "\xE2\x80\x94";
      if (param(cc).fine && host_.paramValue(kFineParamId) >= 0.5) {
         char text[16];
         std::snprintf(text, sizeof(text), "%.2f", std::clamp(v, 0.0, 127.0));
         return text;
      }
      return std::to_string(toCcValue(v));
   }
   template <typename Control>
   void wire(Control &c, int cc, unsigned accent) {
      c.setAccent(accent);
      c.valueText = [this, cc](double v) { return valueText(cc, v); };
      bindings_.bind(c, static_cast<clap_id>(cc));
      controls_.push_back({&c, cc, accent});
   }
   // The CC under a control.
   static std::string ccCaption(int cc) {
      return "CC " + std::to_string(cc);
   }

   // A knob for a CC; label nullptr uses the CC's short label, "" shows none.
   ui::Knob &knob(int cc, unsigned accent, Rect cell, const char *label = nullptr) {
      auto &k = add<ui::Knob>(cell, rangeOf(cc));
      k.setLabel(label ? label : param(cc).label);
      k.setCaption(ccCaption(cc));
      wire(k, cc, accent);
      return k;
   }
   // A vertical slider in a column `cell` wide: its name above, its value and CC number below.
   ui::Slider &vslider(int cc, unsigned accent, Rect cell, float top, float height, const char *name = nullptr) {
      label(name ? name : param(cc).label, {cell.x - 6, top, cell.w + 12, 12}, kDim, visage::Font::kCenter, 0.8f);
      auto &s = add<ui::Slider>(Rect{cell.x + cell.w * 0.18f, top + 16, cell.w * 0.64f, height},
                                ui::Slider::Orientation::Vertical, rangeOf(cc));
      wire(s, cc, accent);
      s.valueText = [](double) { return std::string(); };
      readouts_.push_back({&label("", {cell.x - 6, top + height + 20, cell.w + 12, 13}, kWhite, visage::Font::kCenter, 0.85f), cc});
      label(std::to_string(cc), {cell.x - 6, top + height + 36, cell.w + 12, 11}, kDim, visage::Font::kCenter, 0.8f);
      return s;
   }
   ui::Label &label(const std::string &text, Rect r, unsigned color = kWhite,
                    visage::Font::Justification j = visage::Font::kLeft, float fontScale = 0.78f) {
      return add<ui::Label>(r, text, color, j, fontScale);
   }
   // Small grey notes, such as a section's right-hand remark.
   ui::Label &hint(const std::string &text, Rect r, visage::Font::Justification j = visage::Font::kRight) {
      return label(text, r, kHint, j, 0.8f);
   }
   ui::Label &caption(int cc, Rect r, visage::Font::Justification j = visage::Font::kCenter) {
      return label(ccCaption(cc), r, kDim, j, 0.8f);
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
      auto &bar = add<ui::PresetBar>({(kDesignWidth - 480) * 0.5f - 60, 18, 480, 28}, factoryPresets(), kPresetFormat, host_);
      bar.setAccent(kOrange);
      bar.defaults = presetDefaults();
      bar.currentName = [this] { return host_.presetName(); };
      bar.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      add<ui::UpdateNotice>({300, 20, 220, 24});

      auto &led = add<ui::ActivityLed>({1150, 22, 112, 20}, "FROM SYNTH");
      led.setColor(kGreen);
      heardLed_ = &led;
      tips_.add(led, "Lights when a CC comes in on the MIDI channel: the synth's own knobs, with its MIDI out "
                     "routed into this track, or another controller.");
      place(fine_, {1272, 18, 70, 28});
      place(follow_, {1348, 18, 76, 28});
      fine_.setAccent(kOrange);
      follow_.setAccent(kGreen);
      bindings_.bind(fine_, kFineParamId);
      bindings_.bind(follow_, kFollowParamId);
      tips_.add(fine_, "Send the 14-bit controls' fine byte too (on CC + 32). Match the synth's Enable 14-bit "
                       "automation setting (Settings row E).");
      tips_.add(follow_, "Move the knobs to the CCs coming in, and tell the host so it can record them.");

      place(chDown_, {1432, 18, 30, 28});
      place(channel_, {1466, 18, 116, 28});
      place(chUp_, {1586, 18, 30, 28});
      place(sendAll_, {1624, 18, 160, 28});
      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
   }

   // ---- row 1: the oscillators ----

   // Five columns like the synth: tuning (fine above coarse), then each wave's level on the bottom
   // row with its enhancer (or, for the sub, its waveform) above it.
   void buildVco(int column, int number, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list); // sub, saw, square, tri, coarse, fine, ultrasaw, pw, metal, wave
      const float x = columnX(kRow1W, column), y = kRow1Y, w = kRow1W[column];
      section(number == 1 ? "VCO 1" : "VCO 2", {x, y, w, kRow1H});
      hint("TOP KNOBS SHAPE THE ONE BELOW", {x + 80, y + 10, w - 94, 12});
      const int top[5] = {ccs[5], ccs[9], ccs[6], ccs[7], ccs[8]};
      const int bottom[5] = {ccs[4], ccs[0], ccs[1], ccs[2], ccs[3]};
      const float cw = (w - 24) / 5;
      for (int c = 0; c < 5; ++c) {
         knob(top[c], c == 0 ? kWhite : color, {x + 12 + c * cw, y + 30, cw, 98});
         knob(bottom[c], c == 0 ? kWhite : color, {x + 12 + c * cw, y + 132, cw, 98});
      }
      picture<vis::VcoWave>({x + 12, y + 240, w - 24, 46},
                            vis::VcoWave::Ccs{ccs[0], ccs[1], ccs[2], ccs[3], ccs[6], ccs[7], ccs[8]}, color);
   }

   void buildVco3() {
      const float x = columnX(kRow1W, 2), y = kRow1Y, w = kRow1W[2];
      section("VCO 3 \xC2\xB7 LFO 3", {x, y, w, kRow1H});
      knob(22, kPurple, {x + 14, y + 30, w - 28, 120});
      hint("WAVE, DIV AND KBD", {x + 8, y + 156, w - 16, 12}, visage::Font::kCenter);
      hint("TRACK ARE BUTTONS", {x + 8, y + 170, w - 16, 12}, visage::Font::kCenter);
      divider({x + 12, y + 196, w - 24, 1});
      label("NOISE", {x + 12, y + 206, w - 24, 18}, kNoise, visage::Font::kCenter);
      hint("TYPE IS A BUTTON", {x + 8, y + 230, w - 16, 12}, visage::Font::kCenter);
      hint("LEVEL: MIXER", {x + 8, y + 244, w - 16, 12}, visage::Font::kCenter);
   }

   void buildAudioMod() {
      const float x = columnX(kRow1W, 3), y = kRow1Y, w = kRow1W[3];
      section("AUDIO MOD", {x, y, w, kRow1H});
      const int ccs[4] = {77, 78, 79, 80};
      const unsigned colors[4] = {kOrange, kPurple, kPurple, kNoise};
      for (int i = 0; i < 4; ++i)
         knob(ccs[i], colors[i], {x + 8 + (i % 2) * 84, y + 34 + (i / 2) * 124, 84, 110});
      tips_.add(*controls_[controls_.size() - 3].control, "VCO 3 modulates VCO 1 (below the centre) or VCO 2 (above it).");
      tips_.add(*controls_[controls_.size() - 2].control, "VCO 3 modulates the Steiner (below the centre) or the ladder filter (above it).");
      tips_.add(*controls_[controls_.size() - 1].control, "Noise modulates VCO 1 (below the centre) or the Steiner filter (above it).");
   }

   // ---- row 2: mixer and filters ----

   void buildMixer() {
      const float x = columnX(kRow2W, 0), y = kRow2Y, w = kRow2W[0];
      section("MIXER", {x, y, w, kRow2H});
      const int ccs[5] = {18, 19, 20, 21, 89};
      const unsigned colors[5] = {kOrange, kBlue, kPurple, kNoise, kWhite};
      picture<vis::MixBar>({x + 140, y + 11, w - 154, 10}, ccs, colors);
      const float cw = (w - 24) / 5;
      for (int i = 0; i < 5; ++i)
         vslider(ccs[i], colors[i], {x + 12 + i * cw, 0, cw, 0}, y + 32, 186);
      hint("EACH SOURCE'S VCF 1 / VCF 2 ROUTING IS A BUTTON", {x + 12, y + 270, w - 24, 12}, visage::Font::kCenter);
   }

   // Cutoff, resonance, brute factor on top; drive, env amount and out below; the response above.
   void buildFilter(int column, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list);
      const float x = columnX(kRow2W, column), y = kRow2Y, w = kRow2W[column];
      section(title, {x, y, w, kRow2H});
      hint("MODE, SLOPE: BUTTONS", {x + w - 140, y + 10, 126, 12});
      picture<vis::FilterCurve>({x + 12, y + 30, w - 24, 92}, ccs[0], ccs[1], ccs[4], color);
      const float cw = (w - 24) / 3;
      for (int i = 0; i < 6; ++i)
         knob(ccs[i], color, {x + 12 + (i % 3) * cw, y + 126 + (i / 3) * 86, cw, 84});
   }

   void buildMasterCutoff() {
      const float x = columnX(kRow2W, 2), y = kRow2Y, w = kRow2W[2];
      section("MASTER", {x, y, w, kRow2H});
      knob(27, kWhite, {x + 8, y + 60, w - 16, 140}, "CUTOFF");
      hint("MOVES BOTH", {x + 4, y + 214, w - 8, 12}, visage::Font::kCenter);
      hint("FILTERS", {x + 4, y + 228, w - 8, 12}, visage::Font::kCenter);
      hint("ROUTING: BUTTON", {x + 4, y + 268, w - 8, 12}, visage::Font::kCenter);
   }

   // ---- row 3: performance, LFOs and envelopes ----

   void buildPerformance() {
      const float x = columnX(kRow3W, 0), y = kRow3Y, w = kRow3W[0];
      section("PERFORMANCE", {x, y, w, kRow3H});
      add<vis::Tile>({x + 12, y + 32, 52, 232});
      auto &wheel = add<vis::WheelStrip>({x + 22, y + 44, 32, 182}, rangeOf(1));
      wire(wheel, 1, kWheel);
      label("MOD", {x + 12, y + 232, 52, 14}, kWheel, visage::Font::kCenter, 0.7f);
      caption(1, {x + 12, y + 248, 52, 12});
      knob(5, kWhite, {x + 72, y + 30, 60, 84});
      knob(119, kWhite, {x + 136, y + 30, 60, 84}, "BEND");
      label("MACROS", {x + 72, y + 126, 120, 14}, kDim, visage::Font::kCenter, 0.8f);
      const int macros[4] = {11, 2, 4, 3};
      for (int i = 0; i < 4; ++i)
         knob(macros[i], kPink, {x + 72 + (i % 2) * 64, y + 140 + (i / 2) * 84, 60, 82});
      hint("M3 / M4 DOUBLE AS THE", {x + 12, y + 316, w - 24, 12}, visage::Font::kLeft);
      hint("EXPRESSION PEDALS", {x + 12, y + 330, w - 24, 12}, visage::Font::kLeft);
   }

   void buildLfo(int index, int rate, int second, unsigned color) {
      const float x = columnX(kRow3W, 1 + index), y = kRow3Y, w = kRow3W[1 + index];
      section(index == 0 ? "LFO 1" : "LFO 2", {x, y, w, kRow3H});
      auto &led = add<ui::ActivityLed>({x + w - 28, y + 9, 16, 16});
      led.setColor(color);
      led.fadeMs = 0;
      led.setLevel(1.0f);
      leds_[index] = &led;
      knob(rate, color, {x + 10, y + 36, w - 20, 120});
      knob(second, color, {x + 10, y + 166, w - 20, 120});
      hint("WAVE, SYNC AND", {x + 6, y + 306, w - 12, 12}, visage::Font::kCenter);
      hint("RETRIG: BUTTONS", {x + 6, y + 320, w - 12, 12}, visage::Font::kCenter);
   }

   // The synth's envelopes are sliders: the shape drawn from them, then one slider per stage.
   void buildEnvelope(int index, std::initializer_list<int> list, int velocity, const char *velocityLabel) {
      const std::vector<int> st(list); // delay (or -1), attack, decay, sustain, release
      const float x = columnX(kRow3W, 3 + index), y = kRow3Y, w = kRow3W[3 + index];
      const unsigned color = kEnvColors[index];
      section(index == 0 ? "ENV 1 \xC2\xB7 FILTERS" : index == 1 ? "ENV 2 \xC2\xB7 VCA" : "ENV 3", {x, y, w, kRow3H});
      picture<vis::EnvShape>({x + 12, y + 32, w - 24, 90}, st[0], st[1], st[2], st[3], st[4], color);
      std::vector<std::pair<int, const char *>> sliders;
      if (st[0] >= 0)
         sliders.push_back({st[0], "DLY"});
      sliders.push_back({st[1], "A"});
      sliders.push_back({st[2], "D"});
      sliders.push_back({st[3], "S"});
      sliders.push_back({st[4], "R"});
      if (velocity >= 0)
         sliders.push_back({velocity, "VEL"});
      const float cw = (w - 24) / static_cast<float>(sliders.size());
      for (size_t i = 0; i < sliders.size(); ++i) {
         auto &s = vslider(sliders[i].first, sliders[i].first == velocity ? kWhite : color, {x + 12 + i * cw, 0, cw, 0},
                           y + 132, 180, sliders[i].second);
         if (sliders[i].first == velocity)
            tips_.add(s, std::string(velocityLabel) + ": how much velocity scales this envelope (CC " +
                            std::to_string(velocity) + ").");
      }
   }

   // ---- right: effects, matrix and sequencer ----

   void buildFx() {
      const float x = kRightX, y = kRow1Y, w = kRightW;
      section("ANALOG EFFECTS", {x, y, w, kRow1H});
      hint("THE EFFECT'S MODE IS A BUTTON ON THE SYNTH", {x + 200, y + 10, w - 214, 12});
      picture<vis::FxTaps>({x + 12, y + 30, w - 24, 112}, kGreen);
      const int ccs[5] = {112, 113, 114, 115, 31};
      const float cw = (w - 24) / 5;
      for (int i = 0; i < 5; ++i)
         knob(ccs[i], i == 4 ? kWhite : kGreen, {x + 12 + i * cw, y + 156, cw, 124});
   }

   void buildMatrix() {
      const float x = kRightX, y = kRow2Y, w = kRightW - 120 - kGutter, h = kRow2H + kGutter + kRow3H;
      section("MATRIX", {x, y, w, h});
      hint("ROUTING ISN'T ON MIDI: SET IT ON THE SYNTH", {x + 120, y + 10, w - 134, 12});
      auto &map = add<vis::MatrixMap>({x + 12, y + 36, w - 24, 400});
      map.setAccents(kMatrixBlue, kPink);
      // What the plugin knows about the synth.
      const float ty = y + 452;
      add<vis::Tile>({x + 12, ty, w - 24, h - (ty - y) - 12});
      label("FROM THE SYNTH", {x + 26, ty + 12, 200, 16}, kGreen);
      status_ = &label("", {x + 26, ty + 36, w - 52, 16}, kWhite, visage::Font::kLeft, 0.85f);
      knownCount_ = &label("", {x + 26, ty + 58, w - 52, 16}, kDim, visage::Font::kLeft, 0.85f);
      hint("A DIM KNOB IS A GUESS: NOT HEARD OR SET SINCE THE LAST PRESET CHANGE.", {x + 26, ty + 86, w - 52, 12},
           visage::Font::kLeft);
      hint("TURN IT ON THE SYNTH, WITH ITS MIDI OUT ROUTED INTO THIS TRACK, TO READ IT.", {x + 26, ty + 100, w - 52, 12},
           visage::Font::kLeft);
      hint("PRESETS, BUTTONS AND THE MATRIX AREN'T SENT AS CCS.", {x + 26, ty + 114, w - 52, 12}, visage::Font::kLeft);
   }

   void buildSequencer() {
      const float w = 120, x = kRightX + kRightW - w, y = kRow2Y, h = kRow2H + kGutter + kRow3H;
      section("SEQ", {x, y, w, h});
      const int ccs[3] = {116, 117, 118};
      for (int i = 0; i < 3; ++i)
         knob(ccs[i], kRed, {x + 10, y + 36 + i * 140, w - 20, 124});
      hint("STEPS, ARP AND", {x + 6, y + h - 60, w - 12, 12}, visage::Font::kCenter);
      hint("TRANSPORT ARE", {x + 6, y + h - 46, w - 12, 12}, visage::Font::kCenter);
      hint("ON THE SYNTH", {x + 6, y + h - 32, w - 12, 12}, visage::Font::kCenter);
   }

   // ---- keeping it current ----

   void sync() {
      channel_.setText("MIDI CH " + std::to_string(host_.channel()));
      fine_.setText("14-BIT");
      follow_.setText("FOLLOW");
      heardLed_->setCount(host_.heardCount());

      // Knobs the plugin doesn't know go grey, and their number becomes a dash.
      int known = 0;
      const bool fine = host_.paramValue(kFineParamId) >= 0.5;
      for (Control &c : controls_) {
         const bool k = host_.known(c.cc);
         known += k ? 1 : 0;
         if (k != c.shownKnown || fine != c.shownFine) {
            c.shownKnown = k;
            c.shownFine = fine;
            c.control->setAccent(k ? c.accent : kUnknown);
            c.control->redraw();
         }
      }
      for (auto [readout, cc] : readouts_)
         readout->setText(host_.known(cc) ? std::to_string(toCcValue(host_.paramModulated(static_cast<clap_id>(cc))))
                                          : "\xE2\x80\x94");
      const int program = host_.heardProgram();
      status_->setText(program < 0 ? "NO PRESET CHANGE HEARD SINCE THE PLUGIN OPENED"
                                   : "THE SYNTH CHANGED PRESET (PROGRAM CHANGE " + std::to_string(program + 1) + ")");
      knownCount_->setText(std::to_string(known) + " OF " + std::to_string(kCcParamCount) + " KNOBS KNOWN");
   }

   // The rate lights blink at a guess of each LFO's speed (the synth's rate curve isn't published).
   void blinkLfos() {
      const int rates[2] = {91, 93};
      for (int i = 0; i < 2; ++i) {
         const double speed = host_.paramModulated(static_cast<clap_id>(rates[i])) / 127.0;
         const double hz = 0.1 * std::pow(2.0, 8.0 * speed);
         lfoPhase_[i] = std::fmod(lfoPhase_[i] + hz * 0.03, 1.0);
         leds_[i]->setLevel(lfoPhase_[i] < 0.5 ? 1.0f : 0.15f);
      }
   }

   struct Control {
      ui::ValueControl *control;
      int cc;
      unsigned accent;
      bool shownKnown = true, shownFine = false;
   };

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   std::vector<Control> controls_;
   std::vector<vis::Picture *> pictures_;
   std::vector<std::pair<ui::Label *, int>> readouts_;
   ui::ActivityLed *leds_[2] = {};
   ui::ActivityLed *heardLed_ = nullptr;
   ui::Label *status_ = nullptr, *knownCount_ = nullptr;
   double lfoPhase_[2] = {};
   ui::Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
   ui::Button fine_{"14-BIT"}, follow_{"FOLLOW"};
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

} // namespace mb
