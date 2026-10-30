// Visage panel for the MatrixBrute controller, laid out on a fixed kDesignWidth x kDesignHeight grid
// and scaled to the window, which keeps its aspect ratio.
//
// It follows the synth's own panel, section by section and knob by knob: VCO 1 and 2 with their
// enhancers above the wave levels down the left; the mixer; the Steiner and ladder filters with
// Master Cutoff between them; the three envelopes' sliders; and the analog effects on the right.
// A row along the bottom of the panel holds VCO 3 and audio mod, the LFOs, the preset display and
// the sequencer. Along the front edge, like the synth's, are glide, the mod wheel, bend range and
// the macros. Every knob and slider is one CC. The synth's buttons and its matrix aren't on MIDI,
// so they're left out.
// Knobs the plugin hasn't heard from the synth or set itself are drawn dim, since their value is a
// guess.

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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cc_map.h"
#include "patches.h"
#include "synth_link.h"
#include "visuals.h"

namespace mb {
namespace {

namespace ui = jclap::ui;
namespace vis = visuals;
using ui::Rect;

constexpr float kDesignWidth = 1584.0f;
constexpr float kDesignHeight = 1072.0f;
constexpr float kHeaderHeight = 64.0f;
// The window opens at this fraction of the design size; right-click the background for others.
constexpr float kOpenScale = 0.75f;

// Five columns, in the synth's order, with 16 point margins and 12 point gutters; then the front edge.
constexpr float kMargin = 16.0f, kGutter = 12.0f;
constexpr float kColW[] = {500, 184, 280, 320, 220};
constexpr float colX(int column) {
   float x = kMargin;
   for (int i = 0; i < column; ++i)
      x += kColW[i] + kGutter;
   return x;
}
static_assert(colX(4) + kColW[4] + kMargin == kDesignWidth, "the columns fill the width");
// Every knob cell is the same height, so every knob is the same size.
constexpr float kKnobH = 104;
// Three rows: VCO 1's, VCO 2's (the mixer, filters, envelopes and effects span both), and a bottom
// row that every column shares: VCO 3 and audio mod, the LFOs, the preset and the sequencer.
constexpr float kTop = kHeaderHeight + kMargin;
constexpr float kRowH = 340, kBottomH = 150;
constexpr float kUpperH = 2 * kRowH + kGutter;
constexpr float kBodyH = kUpperH + kGutter + kBottomH;
constexpr float kRow2Y = kTop + kRowH + kGutter;
constexpr float kRow3Y = kTop + kUpperH + kGutter;
constexpr float kLipY = kTop + kBodyH + kGutter, kLipH = 110;
static_assert(kLipY + kLipH + kMargin == kDesignHeight, "the rows fill the height");
// The filters sit at the top and bottom of the upper rows with Master Cutoff between them.
constexpr float kVcfH = 268, kCutoffH = kUpperH - 2 * kVcfH - 2 * kGutter;
static_assert(kCutoffH >= kKnobH + 20, "Master Cutoff's knob fits between the filters");
// The envelopes share the upper rows.
constexpr float kEnvH = (kUpperH - 2 * kGutter) / 3;

constexpr unsigned kAmber = 0xfff2c94c;
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
constexpr unsigned kPink = 0xffff6fb5;
constexpr unsigned kWheel = 0xffd9a6f5;
// The filters' own colours, as printed on the synth: orange for the Steiner, teal for the ladder.
constexpr unsigned kSteiner = 0xfff07a3a;
constexpr unsigned kLadder = 0xff2fd1c5;
constexpr unsigned kEnvColors[] = {kSteiner, kGreen, kPurple};


class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      buildVco(kTop, "VCO 1", kAmber, {9, 10, 12, 13, 66, 65, 68, 69, 70, 67});
      buildVco(kRow2Y, "VCO 2", kBlue, {14, 15, 16, 17, 72, 71, 74, 75, 76, 73});
      buildVco3();
      buildAudioMod();
      buildMixer();
      buildLfo1();
      buildFilter(kTop, "VCF1 STEINER", kSteiner, {81, 23, 83, 82, 24, 7});
      buildMasterCutoff();
      buildFilter(kTop + kUpperH - kVcfH, "VCF2 LADDER", kLadder, {85, 25, 87, 86, 26, 8});
      buildLfo2();
      buildPreset();
      buildEnvelope(0, "ENV 1 (VCF)", {-1, 102, 103, 28, 104}, 94, "VELO / VCF");
      buildEnvelope(1, "ENV 2 (VCA)", {-1, 105, 106, 29, 107}, 95, "VELO / VCA");
      buildEnvelope(2, "ENV 3", {108, 109, 110, 30, 111}, -1, "");
      buildFx();
      buildSequencer();
      buildLip();
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
      canvas.setColor(0xff1d2127);
      canvas.fill(0, 0, width(), height());

      // Title bar.
      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kSteiner);
      canvas.text("Matrix", ui::font(28 * s), visage::Font::kLeft, 64 * s, 10 * s, 100 * s, 44 * s);
      canvas.setColor(t.text);
      canvas.text("Brute", ui::font(28 * s), visage::Font::kLeft, 146 * s, 10 * s, 100 * s, 44 * s);
      canvas.setColor(kHint);
      canvas.text("CC", ui::font(16 * s), visage::Font::kLeft, 222 * s, 18 * s, 40 * s, 30 * s);

      // The front edge, under the panel.
      canvas.setColor(0xff0f1114);
      canvas.fill(0, (kLipY - 6) * s, width(), (kLipH + 6 + kMargin) * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small 4 x 4 grid of matrix buttons with a diagonal lit.
      for (int r = 0; r < 4; ++r)
         for (int c = 0; c < 4; ++c) {
            canvas.setColor(r == c ? kSteiner : 0xff3d444e);
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

   // A knob for a CC, labelled as on the synth.
   ui::Knob &knob(int cc, unsigned accent, Rect cell, const char *label) {
      auto &k = add<ui::Knob>(cell, rangeOf(cc));
      k.setLabel(label);
      k.setCaption("CC " + std::to_string(cc));
      wire(k, cc, accent);
      return k;
   }
   // A vertical slider in a column `cell` wide: its name above, its value and CC number below.
   ui::Slider &vslider(int cc, unsigned accent, Rect cell, float top, float height, const char *name) {
      label(name, {cell.x - 6, top, cell.w + 12, 12}, kWhite, visage::Font::kCenter, 0.85f);
      auto &s = add<ui::Slider>(Rect{cell.x + cell.w * 0.2f, top + 16, cell.w * 0.6f, height},
                                ui::Slider::Orientation::Vertical, rangeOf(cc));
      wire(s, cc, accent);
      s.valueText = [](double) { return std::string(); };
      readouts_.push_back({&label("", {cell.x - 6, top + height + 20, cell.w + 12, 13}, kWhite, visage::Font::kCenter, 0.85f), cc});
      label("CC " + std::to_string(cc), {cell.x - 6, top + height + 36, cell.w + 12, 11}, kDim, visage::Font::kCenter, 0.8f);
      return s;
   }
   ui::Label &label(const std::string &text, Rect r, unsigned color = kWhite,
                    visage::Font::Justification j = visage::Font::kLeft, float fontScale = 0.78f) {
      return add<ui::Label>(r, text, color, j, fontScale);
   }
   // Small grey notes.
   ui::Label &hint(const std::string &text, Rect r, visage::Font::Justification j = visage::Font::kCenter) {
      return label(text, r, kHint, j, 0.8f);
   }
   void section(const std::string &title, Rect r, bool dark, unsigned rule = 0xffc9d0d8) {
      add<vis::HwSection>(r, title, dark, rule);
   }
   template <typename T, typename... Args>
   T &picture(Rect r, Args &&...args) {
      auto &p = add<T>(r, ccValue(), std::forward<Args>(args)...);
      pictures_.push_back(&p);
      return p;
   }
   vis::GateSource gate() {
      return [this] {
         const NoteActivity n = host_.notes();
         const double now = noteClock();
         return vis::Gate{n.held > 0, n.lastOn < 0 ? -1 : now - n.lastOn, n.lastOff < 0 ? -1 : now - n.lastOff};
      };
   }
   vis::CcValue ccValue() {
      return [this](int cc) { return host_.paramModulated(static_cast<clap_id>(cc)); };
   }

   // ---- header ----

   void buildHeader() {
      // The synth's presets: the bank compiled in, until READ SYNTH brings the ones it has now.
      auto &bar = add<ui::PresetBar>({474, 18, 520, 28}, presets_, kPresetFormat, host_);
      bar_ = &bar;
      bar.setAccent(kSteiner);
      bar.defaults = presetDefaults();
      bar.currentName = [this] { return host_.presetName(); };
      bar.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      bar.onLoad = [this](const ui::Preset &p) {
         if (const HardwarePatch *patch = findPatch(patches_, p.name))
            host_.loadProgram(*patch);
         else
            ui::applyPreset(p, host_, presetDefaults());
      };
      add<ui::UpdateNotice>({272, 20, 190, 24});

      const float r = kDesignWidth - kMargin;
      place(sendAll_, {r - 160, 18, 160, 28});
      place(chUp_, {r - 198, 18, 30, 28});
      place(channel_, {r - 318, 18, 116, 28});
      place(chDown_, {r - 352, 18, 30, 28});
      place(follow_, {r - 440, 18, 80, 28});
      place(fine_, {r - 516, 18, 70, 28});
      fine_.setAccent(kSteiner);
      follow_.setAccent(kGreen);
      bindings_.bind(fine_, kFineParamId);
      bindings_.bind(follow_, kFollowParamId);
      tips_.add(fine_, "Send the 14-bit controls' fine byte too (on CC + 32). Match the synth's Enable 14-bit "
                       "automation setting (Settings row E).");
      tips_.add(follow_, "Move the knobs to the CCs coming in, and tell the host so it can record them.");
      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
   }

   // ---- column 1: oscillators, audio mod, wheels ----

   // Five columns as on the synth: Fine above Coarse, then each wave's level on the bottom row with
   // its enhancer (for the sub, its waveform) above it; the wave mix drawn underneath.
   void buildVco(float y, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list); // sub, saw, square, tri, coarse, fine, ultrasaw, pw, metal, wave
      const float x = colX(0), w = kColW[0];
      section(title, {x, y, w, kRowH}, true);
      const int top[5] = {ccs[5], ccs[9], ccs[6], ccs[7], ccs[8]};
      const int bottom[5] = {ccs[4], ccs[0], ccs[1], ccs[2], ccs[3]};
      const char *topNames[5] = {"FINE", "SUB WAVE", "ULTRASAW", "PULSE WIDTH", "METALIZER"};
      const char *bottomNames[5] = {"COARSE", "SUB", "SAW", "SQUARE", "TRIANGLE"};
      const float cw = (w - 24) / 5;
      for (int c = 0; c < 5; ++c) {
         knob(top[c], c == 0 ? kWhite : color, {x + 12 + c * cw, y + 24, cw, kKnobH}, topNames[c]);
         knob(bottom[c], c == 0 ? kWhite : color, {x + 12 + c * cw, y + 32 + kKnobH, cw, kKnobH}, bottomNames[c]);
      }
      for (int c = 1; c <= 2; ++c)
         add<ui::Divider>({x + 12 + c * cw, y + 30, 1, 2 * kKnobH - 2});
      const float py = y + 44 + 2 * kKnobH;
      picture<vis::VcoWave>({x + 12, py, w - 24, y + kRowH - 12 - py},
                            vis::VcoWave::Ccs{ccs[0], ccs[1], ccs[2], ccs[3], ccs[6], ccs[7], ccs[8]}, color);
   }

   // VCO 3 and audio mod share the bottom row. Bend range, the wheels' setting, is on the front edge
   // by the mod wheel.
   static constexpr float kVco3W = 120;
   void buildVco3() {
      const float x = colX(0), y = kRow3Y, w = kVco3W;
      section("VCO 3 - LFO 3", {x, y, w, kBottomH}, true);
      knob(22, kPurple, {x + (w - 100) * 0.5f, y + 28, 100, kKnobH}, "COARSE");
   }

   void buildAudioMod() {
      const float x = colX(0) + kVco3W + kGutter, y = kRow3Y, w = kColW[0] - kVco3W - kGutter;
      section("AUDIO MOD", {x, y, w, kBottomH}, false);
      const int ccs[4] = {77, 78, 79, 80};
      const char *names[4] = {"VCO1 > VCO2", "VCO3 > 1 | 2", "VCO3 > F1 | F2", "NOISE > 1 | F1"};
      const unsigned colors[4] = {kAmber, kPurple, kPurple, kNoise};
      const float cw = (w - 8) / 4;
      for (int i = 0; i < 4; ++i)
         knob(ccs[i], colors[i], {x + 4 + i * cw, y + 28, cw, kKnobH}, names[i]);
   }

   // ---- column 2: mixer and LFO 1 ----

   void buildMixer() {
      const float x = colX(1), y = kTop, w = kColW[1], h = kUpperH;
      section("MIXER", {x, y, w, h}, true);
      const int ccs[5] = {18, 19, 20, 21, 89};
      const char *names[5] = {"VCO 1", "VCO 2", "VCO 3", "NOISE", "EXTERNAL"};
      const unsigned colors[5] = {kAmber, kBlue, kPurple, kNoise, kWhite};
      const float rh = (h - 70) / 5;
      for (int i = 0; i < 5; ++i)
         knob(ccs[i], colors[i], {x + (w - 120) * 0.5f, y + 26 + i * rh + (rh - kKnobH) * 0.5f, 120, kKnobH}, names[i]);
      picture<vis::MixBar>({x + 12, y + h - 26, w - 24, 10}, ccs, colors);
   }

   void buildLfo1() {
      const float x = colX(1), y = kRow3Y, w = kColW[1], cw = (w - 16) / 2;
      section("LFO 1", {x, y, w, kBottomH}, false);
      knob(90, kCyan, {x + 8, y + 28, cw, kKnobH}, "PHASE");
      knob(91, kCyan, {x + 8 + cw, y + 28, cw, kKnobH}, "RATE");
      leds_[0] = &rateLed({x + w - 22, y + 26}, kCyan);
   }

   ui::ActivityLed &rateLed(ui::Rect at, unsigned color) {
      auto &led = add<ui::ActivityLed>({at.x, at.y, 14, 14});
      led.setColor(color);
      led.fadeMs = 0;
      led.setLevel(1.0f);
      return led;
   }

   // ---- column 3: filters, Master Cutoff and LFO 2 ----

   // Drive, cutoff and resonance on top; brute factor, env amount and out below.
   void buildFilter(float y, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list);
      const float x = colX(2), w = kColW[2];
      section(title, {x, y, w, kVcfH}, true, color);
      const char *names[6] = {"DRIVE", "CUTOFF", "RESONANCE", "BRUTE", "ENV AMT", ""};
      const float cw = (w - 16) / 3;
      for (int i = 0; i < 6; ++i)
         knob(ccs[i], color, {x + 8 + (i % 3) * cw, y + 28 + (i / 3) * (kKnobH + 26), cw, kKnobH},
              i == 5 ? (ccs[5] == 7 ? "STEINER OUT" : "LADDER OUT") : names[i]);
   }

   void buildMasterCutoff() {
      const float x = colX(2), y = kTop + kVcfH + kGutter, w = kColW[2], h = kCutoffH;
      add<vis::Tile>({x, y + 4, w, h - 4});
      knob(27, kWhite, {x + 6, y + 4 + (h - 4 - kKnobH) * 0.5f, 116, kKnobH}, "MASTER CUTOFF");
      picture<vis::DualFilter>({x + 126, y + 16, w - 138, h - 24}, vis::DualFilter::Filter{23, 83, 24, kSteiner},
                               vis::DualFilter::Filter{25, 87, 26, kLadder});
   }

   void buildLfo2() {
      const float x = colX(2), y = kRow3Y, w = kColW[2];
      section("LFO 2", {x, y, w, kBottomH}, false);
      knob(92, kPink, {x + 30, y + 28, 100, kKnobH}, "DELAY");
      knob(93, kPink, {x + w - 130, y + 28, 100, kKnobH}, "RATE");
      leds_[1] = &rateLed({x + w - 22, y + 26}, kPink);
   }

   // ---- column 4: preset display and envelopes ----

   // The synth's preset, as far as the plugin knows, and the read of every preset stored on it.
   void buildPreset() {
      const float x = colX(3), y = kRow3Y, w = kColW[3], h = kBottomH;
      section("PRESET", {x, y, w, h}, true);
      display_ = &add<vis::PresetDisplay>({x + w - 170, y + 36, 156, 52});
      place(readSynth_, {x + 14, y + 44, 140, 30});
      readSynth_.setAccent(kSteiner);
      readSynth_.onClick = [] { SynthLink::get().requestRead(); };
      tips_.add(readSynth_, "Reads every preset stored on the synth into the preset bar (about a minute). In Bitwig "
                            "this needs the MatrixBrute presets controller script (bitwig/MatrixBrute.control.js), "
                            "since a plugin there can't send or receive SysEx.");
      linkStatus_ = &label("", {x + 14, y + 104, w - 28, 20}, kDim, visage::Font::kLeft, 0.8f);
   }

   // The synth's envelopes are sliders: velocity (or ENV 3's delay) in white on the left, then
   // attack, decay, sustain and release, with the shape they make drawn above.
   void buildEnvelope(int index, const char *title, std::initializer_list<int> list, int velocity, const char *velocityName) {
      const std::vector<int> st(list); // delay (or -1), attack, decay, sustain, release
      const float x = colX(3), w = kColW[3], h = kEnvH, y = kTop + index * (h + kGutter);
      const unsigned color = kEnvColors[index];
      section(title, {x, y, w, h}, false);
      picture<vis::EnvShape>({x + 12, y + 24, w - 24, 46}, st[0], st[1], st[2], st[3], st[4], color, gate());
      const float cw = (w - 24) / 5, top = y + 80, sh = h - (top - y) - 56;
      add<vis::Tile>({x + 10, top - 4, cw + 4, h - (top - y) - 4});
      const int first = velocity >= 0 ? velocity : st[0];
      vslider(first, kWhite, {x + 12, 0, cw, 0}, top, sh, velocity >= 0 ? velocityName : "DELAY");
      const char *names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      for (int i = 0; i < 4; ++i)
         vslider(st[1 + i], color, {x + 12 + (i + 1) * cw, 0, cw, 0}, top, sh, names[i]);
   }

   // ---- column 5: analog effects and sequencer ----

   void buildFx() {
      const float x = colX(4), y = kTop, w = kColW[4], h = kUpperH;
      section("ANALOG FX", {x, y, w, h}, true);
      const float tapsH = 110;
      picture<vis::FxTaps>({x + 10, y + 26, w - 20, tapsH}, kCyan);
      const float top = y + 26 + tapsH, gap = (h - (top - y) - 3 * kKnobH - 8) / 4;
      const int ccs[5] = {112, 113, 114, 115, 31};
      const char *names[5] = {"TIME", "REGEN", "TONE / RATE", "WIDTH", "DRY / WET"};
      const float cw = (w - 12) / 2;
      for (int i = 0; i < 5; ++i) {
         const float cx = i == 4 ? x + (w - cw) * 0.5f : x + 6 + (i % 2) * cw;
         knob(ccs[i], i == 4 ? kWhite : kCyan, {cx, top + gap + (i / 2) * (kKnobH + gap), cw, kKnobH}, names[i]);
      }
   }

   void buildSequencer() {
      const float x = colX(4), y = kRow3Y, w = kColW[4], h = kBottomH;
      section("SEQUENCER", {x, y, w, h}, true);
      const float cw = (w - 12) / 3;
      knob(116, kRed, {x + 6, y + 28, cw, kKnobH}, "RATE");
      knob(117, kRed, {x + 6 + cw, y + 28, cw, kKnobH}, "SWING");
      knob(118, kRed, {x + 6 + 2 * cw, y + 28, cw, kKnobH}, "GATE");
   }

   // ---- the front edge: glide, mod wheel, macros, and what the plugin knows ----

   void buildLip() {
      const float x = kMargin, y = kLipY;
      knob(5, kWhite, {x + 8, y + 2, 92, kKnobH}, "GLIDE");
      auto &wheel = add<vis::WheelStrip>({x + 116, y + 8, 30, 92}, rangeOf(1));
      wire(wheel, 1, kWheel);
      label("MOD WHEEL", {x + 154, y + 30, 90, 14}, kWheel, visage::Font::kLeft, 0.8f);
      wheelValue_ = &label("", {x + 154, y + 48, 90, 16}, kWhite, visage::Font::kLeft, 0.8f);
      label("CC 1", {x + 154, y + 68, 90, 12}, kDim, visage::Font::kLeft, 0.8f);
      knob(119, kWhite, {x + 244, y + 2, 92, kKnobH}, "BEND RANGE");
      const int macros[4] = {11, 2, 4, 3};
      const char *names[4] = {"M1", "M2", "M3 / EXP 1", "M4 / EXP 2"};
      for (int i = 0; i < 4; ++i)
         knob(macros[i], kWhite, {x + 360 + i * 96, y + 2, 92, kKnobH}, names[i]);
      const float sx = x + 770;
      auto &led = add<ui::ActivityLed>({sx, y + 14, 130, 18}, "FROM SYNTH");
      led.setColor(kGreen);
      heardLed_ = &led;
      tips_.add(led, "Lights when a CC comes in on the MIDI channel: the synth's own knobs, with its MIDI out "
                     "routed into this track, or another controller.");
      knownCount_ = &label("", {sx + 140, y + 14, 300, 18}, kWhite, visage::Font::kLeft, 0.85f);
      status_ = &label("", {sx + 440, y + 14, kDesignWidth - kMargin - sx - 440, 18}, kDim, visage::Font::kLeft, 0.85f);
      hint("A DIM KNOB IS A GUESS: NOT HEARD OR SET SINCE THE SYNTH'S LAST PRESET CHANGE. TURN IT ON THE SYNTH TO READ IT.",
           {sx, y + 46, kDesignWidth - kMargin - sx, 12}, visage::Font::kLeft);
      hint("PICK ONE OF THE SYNTH'S PRESETS IN THE PRESET BAR TO LOAD IT; READ SYNTH FETCHES THE ONES STORED ON IT NOW.",
           {sx, y + 62, kDesignWidth - kMargin - sx, 12}, visage::Font::kLeft);
   }

   // ---- keeping it current ----

   void sync() {
      syncLink();
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
      const int program = host_.synthPreset();
      const std::string code = program < 0 ? "---" : presetCode(program);
      status_->setText(program < 0 ? "NO SYNTH PRESET PICKED OR HEARD SINCE THE PLUGIN OPENED"
                                   : "THE SYNTH IS ON PRESET " + code);
      knownCount_->setText(std::to_string(known) + " OF " + std::to_string(kCcParamCount) + " KNOBS KNOWN");
      wheelValue_->setText(host_.known(1) ? std::to_string(toCcValue(host_.paramModulated(1))) : "\xE2\x80\x94");

      // The synth's display shows its preset, which the plugin knows when it picked it or heard the
      // Program Change.
      display_->setText(code);
   }

   // The read from the synth, through the Bitwig script: its progress, and the presets once it's done.
   void syncLink() {
      SynthLink &link = SynthLink::get();
      const SynthLink::Status st = link.status();
      std::string text;
      if (!st.connected)
         text = "NEEDS THE BITWIG SCRIPT";
      else if (st.reading)
         text = "READING... " + std::to_string(st.found) + " PRESETS";
      else if (!st.error.empty())
         text = "READ FAILED: " + st.error;
      else if (link.version() > 0)
         text = std::to_string(st.found) + " PRESETS FROM THE SYNTH" + (st.failed ? " (" + std::to_string(st.failed) + " MISSED)" : "");
      else
         text = "READY TO READ";
      linkStatus_->setText(text);
      if (link.version() == shownLinkVersion_)
         return;
      shownLinkVersion_ = link.version();
      std::vector<HardwarePatch> read = link.presets();
      if (read.empty())
         return;
      patches_ = std::move(read);
      presets_ = toPresets(patches_);
      bar_->rescan();
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
   SynthLink::Use linkUse_;
   // What the preset bar lists (it keeps a reference) and what each entry loads.
   std::vector<HardwarePatch> patches_ = factoryBank();
   std::vector<ui::Preset> presets_ = factoryPresets();
   uint32_t shownLinkVersion_ = 0;
   ui::PresetBar *bar_ = nullptr;
   ui::Label *linkStatus_ = nullptr;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   std::vector<Control> controls_;
   std::vector<vis::Picture *> pictures_;
   std::vector<std::pair<ui::Label *, int>> readouts_;
   ui::ActivityLed *leds_[2] = {};
   ui::ActivityLed *heardLed_ = nullptr;
   ui::Label *status_ = nullptr, *knownCount_ = nullptr, *wheelValue_ = nullptr;
   vis::PresetDisplay *display_ = nullptr;
   double lfoPhase_[2] = {};
   ui::Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
   ui::Button fine_{"14-BIT"}, follow_{"FOLLOW"}, readSynth_{"READ SYNTH"};
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth * kOpenScale), static_cast<int>(kDesignHeight * kOpenScale)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath) {
   Panel panel(host);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace mb
