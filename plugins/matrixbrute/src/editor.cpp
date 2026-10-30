// Visage panel for the MatrixBrute controller, laid out on a fixed kDesignWidth x kDesignHeight grid
// and scaled to the window, which keeps its aspect ratio.
//
// It follows the synth's own panel, column by column, with every box sized to its contents on one
// knob pitch: VCO 1 and 2 (the wave mix drawn under them), VCO 3 and noise, over audio mod, VCO
// sync, the wheels and voice; the mixer's column beside the Steiner and ladder filters with Master
// Cutoff and the routing between them, over LFO 1 and LFO 2; the preset display and the three
// envelopes; the 16 x 16 matrix; and the analog effects, the sequencer and, from the synth's left
// end, glide, play control and the macros. As on the synth, a dark upper band sits over a lighter
// strip. Every knob and slider is one CC. The synth's buttons, lights and matrix aren't on MIDI:
// they're drawn as indicators with a state the preset read can set later (setIndicator() and
// MatrixMap::setCell()), and until then they're dim.
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

constexpr float kDesignWidth = 2696.0f;
constexpr float kDesignHeight = 1264.0f;
constexpr float kHeaderHeight = 64.0f;
// The window opens at this fraction of the design size; right-click the background for others.
constexpr float kOpenScale = 0.5f;

// One knob pitch everywhere: a cell is kCellW wide and kKnobH tall, rows kRowPitch apart, and a box
// holding n cells across and r rows down is boxW(n) x boxH(r).
constexpr float kCellW = 92, kKnobH = 100, kRowPitch = 104;
constexpr float kPad = 8, kTitleH = 24;
constexpr float boxW(int n) { return 2 * kPad + n * kCellW; }
constexpr float boxH(int r) { return kTitleH + r * kRowPitch - (kRowPitch - kKnobH) + kPad; }

// The synth's columns, left to right, with 16 point margins and 12 point gutters: oscillators,
// mixer and filters, preset and envelopes, the matrix, and effects and sequencer.
constexpr float kMargin = 16.0f, kGutter = 12.0f;
constexpr float kMixerW = boxW(1), kFilterButtonsW = 110, kFilterW = boxW(3) + kFilterButtonsW;
constexpr float kColW[] = {boxW(4) + 136, kMixerW + kGutter + kFilterW, 290, 808, boxW(5)};
constexpr float colX(int column) {
   float x = kMargin;
   for (int i = 0; i < column; ++i)
      x += kColW[i] + kGutter;
   return x;
}
static_assert(colX(4) + kColW[4] + kMargin == kDesignWidth, "the columns fill the width");
// A dark band over a lighter strip. The band is as tall as two VCOs (their waves under the knobs)
// and VCO 3's row; the strip holds two rows of the oscillators' small sections, the LFOs and ENV 3.
constexpr float kTop = kHeaderHeight + kMargin;
constexpr float kWaveH = 100;
constexpr float kVcoH = boxH(2) + kWaveH + 14, kSmallH = boxH(1);
constexpr float kBandH = 2 * kVcoH + kSmallH + 2 * kGutter;
constexpr float kStripY = kTop + kBandH + kGutter, kStripH = 300, kStripRowH = (kStripH - kGutter) / 2;
static_assert(kStripY + kStripH + kMargin == kDesignHeight, "the band and strip fill the height");
constexpr float kVco2Y = kTop + kVcoH + kGutter, kVco3Y = kVco2Y + kVcoH + kGutter;
// The filters sit at the top and bottom of the band with Master Cutoff and the routing between.
constexpr float kCutoffH = kBandH - 2 * boxH(2) - 2 * kGutter;
// The preset box, then ENV 1 and ENV 2 down the band; ENV 3 is in the strip.
constexpr float kPresetH = 202, kEnvH = (kBandH - kPresetH - 2 * kGutter) / 2;
// The effects, the sequencer and the performance controls down the last column.
constexpr float kFxH = 360, kPerfH = 216, kSeqH = kBandH + kGutter + kStripH - kFxH - kPerfH - 2 * kGutter;

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
      buildVco(kVco2Y, "VCO 2", kBlue, {14, 15, 16, 17, 72, 71, 74, 75, 76, 73});
      buildVco3AndNoise();
      buildAudioModAndSync();
      buildWheelsAndVoice();
      buildMixer();
      buildFilter(kTop, "VCF1 STEINER", kSteiner, {81, 23, 83, 82, 24, 7});
      buildMasterCutoff();
      buildFilter(kTop + kBandH - boxH(2), "VCF2 LADDER", kLadder, {85, 25, 87, 86, 26, 8});
      buildLfo(0, "LFO 1", kCyan, 90, "PHASE", 91);
      buildLfo(1, "LFO 2", kPink, 92, "DELAY", 93);
      buildPreset();
      buildEnvelope(kTop + kPresetH + kGutter, kEnvH, "ENV 1 (VCF)", 0, {-1, 102, 103, 28, 104}, 94, "VELO / VCF");
      buildEnvelope(kTop + kPresetH + kEnvH + 2 * kGutter, kEnvH, "ENV 2 (VCA)", 1, {-1, 105, 106, 29, 107}, 95,
                    "VELO / VCA");
      buildEnvelope(kStripY, kStripH, "ENV 3", 2, {108, 109, 110, 30, 111}, -1, "");
      buildMatrix();
      buildFx();
      buildSequencer();
      buildPerformance();
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

   // A row of knobs across a box from x, one cell each.
   void knobRow(float x, float y, std::initializer_list<int> ccs, std::initializer_list<const char *> names,
                std::initializer_list<unsigned> colors) {
      auto n = names.begin();
      auto c = colors.begin();
      int i = 0;
      for (int cc : ccs) {
         knob(cc, *c, {x + kPad + i * kCellW, y + kTitleH, kCellW, kKnobH}, *n);
         ++i;
         ++n;
         if (c + 1 != colors.end())
            ++c;
      }
   }

   // A button or switch of the synth's that isn't on MIDI, drawn as an indicator under `id`.
   vis::HwButton &indicator(const std::string &id, const std::string &name, std::vector<std::string> lights, Rect r,
                            vis::HwButton::Lights place = vis::HwButton::Lights::Above, unsigned accent = 0xff6fa8ff) {
      auto &b = add<vis::HwButton>(r, name, std::move(lights), place, accent);
      tips_.add(b, name + " is set on the synth: it isn't sent over MIDI. It's dim until the plugin can read it "
                          "from the synth's presets.");
      indicators_.emplace_back(id, &b);
      return b;
   }

public:
   // Shows a hardware button's setting: the lit light (or 1 for on), or -1 for unknown. Nothing
   // feeds these yet; a later change maps them from the synth's presets.
   void setIndicator(const std::string &id, int state) {
      for (auto &[name, button] : indicators_)
         if (name == id)
            button->setState(state);
   }
   // A matrix cell's routing (-1 unknown, 0 none).
   void setMatrixCell(int source, int destination, int state) { matrix_->setCell(source, destination, state); }

private:
   // ---- column 1: oscillators, over audio mod, sync, wheels and voice ----

   // Five columns as on the synth: Fine above Coarse, then each wave's level on the bottom row with
   // its enhancer (for the sub, its waveform) above it; the wave mix drawn under them.
   void buildVco(float y, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list); // sub, saw, square, tri, coarse, fine, ultrasaw, pw, metal, wave
      const float x = colX(0), w = kColW[0], h = kVcoH;
      section(title, {x, y, w, h}, true);
      const int top[5] = {ccs[5], ccs[9], ccs[6], ccs[7], ccs[8]};
      const int bottom[5] = {ccs[4], ccs[0], ccs[1], ccs[2], ccs[3]};
      const char *topNames[5] = {"FINE", "SUB WAVE", "ULTRASAW", "PULSE WIDTH", "METALIZER"};
      const char *bottomNames[5] = {"COARSE", "SUB", "SAW", "SQUARE", "TRIANGLE"};
      const float cw = (w - 2 * kPad) / 5;
      for (int c = 0; c < 5; ++c) {
         knob(top[c], c == 0 ? kWhite : color, {x + kPad + c * cw, y + kTitleH, cw, kKnobH}, topNames[c]);
         knob(bottom[c], c == 0 ? kWhite : color, {x + kPad + c * cw, y + kTitleH + kRowPitch, cw, kKnobH},
              bottomNames[c]);
      }
      for (int c = 1; c <= 2; ++c)
         add<ui::Divider>({x + kPad + c * cw, y + kTitleH + 6, 1, kRowPitch + kKnobH - 12});
      const float py = y + kTitleH + kRowPitch + kKnobH + 6;
      picture<vis::VcoWave>({x + 12, py, w - 24, y + h - 10 - py},
                            vis::VcoWave::Ccs{ccs[0], ccs[1], ccs[2], ccs[3], ccs[6], ccs[7], ccs[8]}, color);
   }

   // VCO 3's coarse tune, wave, LFO divider and keyboard tracking; then the noise type.
   void buildVco3AndNoise() {
      const float x = colX(0), y = kVco3Y, w = 396, h = kSmallH;
      section("VCO 3 - LFO 3", {x, y, w, h}, true);
      knobRow(x, y, {22}, {"COARSE"}, {kPurple});
      const float bx = x + kPad + kCellW, by = y + kTitleH + 4, bh = kKnobH - 8;
      indicator("vco3.wave", "Wave", {"RSAW", "SQR", "TRI", "SIN"}, {bx, by, 104, bh});
      indicator("vco3.lfoDiv", "LFO Div", {"16", "32", "64", "128"}, {bx + 108, by, 104, bh});
      indicator("vco3.kbdTrack", "Kbd Track", {}, {bx + 216, by, w - kPad - (bx + 216 - x), bh});
      const float nx = x + w + kGutter, nw = kColW[0] - w - kGutter;
      section("NOISE", {nx, y, nw, h}, true);
      indicator("noise.type", "Type", {"WHT", "PNK", "RED", "BLU"}, {nx + kPad, by, nw - 2 * kPad, bh});
   }

   void buildAudioModAndSync() {
      const float x = colX(0), y = kStripY, w = boxW(4);
      section("AUDIO MOD", {x, y, w, kStripRowH}, false);
      knobRow(x, y + 6, {77, 78, 79, 80}, {"VCO1 > VCO2", "VCO3 > 1 | 2", "VCO3 > F1 | F2", "NOISE > 1 | F1"},
              {kAmber, kPurple, kPurple, kNoise});
      const float sx = x + w + kGutter, sw = kColW[0] - w - kGutter;
      section("VCO SYNC", {sx, y, sw, kStripRowH}, false);
      indicator("vco.sync", "VCO 2 > VCO 1", {}, {sx + kPad, y + kTitleH + 10, sw - 2 * kPad, kKnobH - 8});
   }

   // Bend range, the mod wheel's routing and the mod wheel itself; then the voice mode.
   void buildWheelsAndVoice() {
      const float x = colX(0), y = kStripY + kStripRowH + kGutter, vw = 140, w = kColW[0] - vw - kGutter;
      section("WHEELS", {x, y, w, kStripRowH}, false);
      knobRow(x, y + 6, {119}, {"BEND RANGE"}, {kWhite});
      const float by = y + kTitleH + 10, bh = kKnobH - 8;
      indicator("wheel.mod", "Mod Wheel", {"MATRIX", "CUTOFF", "LFO1 VIB", "LFO1 AMT"},
                {x + kPad + kCellW + 4, by, 128, bh}, vis::HwButton::Lights::Right);
      const float wx = x + kPad + kCellW + 140;
      auto &wheel = add<vis::WheelStrip>({wx, y + kTitleH + 8, 26, kKnobH - 6}, rangeOf(1));
      wire(wheel, 1, kWheel);
      const float lx = wx + 34, lw = x + w - kPad - lx;
      label("MOD WHEEL", {lx, y + 52, lw, 14}, kWheel, visage::Font::kLeft, 0.75f);
      wheelValue_ = &label("", {lx, y + 72, lw, 16}, kWhite, visage::Font::kLeft, 0.8f);
      label("CC 1", {lx, y + 92, lw, 12}, kDim, visage::Font::kLeft, 0.8f);
      const float vx = x + w + kGutter;
      section("VOICE", {vx, y, vw, kStripRowH}, false);
      indicator("voice.mode", "Mode", {"MONO", "PARA", "DUO"}, {vx + kPad, by, vw - 2 * kPad, bh},
                vis::HwButton::Lights::Right);
   }

   // ---- column 2: mixer, filters, routing and Master Cutoff, over the LFOs ----

   // A narrow column of the five levels, each with its filter routing under it, and the mix drawn at
   // the foot.
   void buildMixer() {
      const float x = colX(1), y = kTop, w = kMixerW, h = kBandH;
      section("MIXER", {x, y, w, h}, true);
      const int ccs[5] = {18, 19, 20, 21, 89};
      const char *names[5] = {"VCO 1", "VCO 2", "VCO 3", "NOISE", "EXTERNAL"};
      const char *ids[5] = {"mixer.vco1.filter", "mixer.vco2.filter", "mixer.vco3.filter", "mixer.noise.filter",
                            "mixer.ext.filter"};
      const unsigned colors[5] = {kAmber, kBlue, kPurple, kNoise, kWhite};
      const float pitch = (h - kTitleH - kPad - 26) / 5;
      for (int i = 0; i < 5; ++i) {
         const float ry = y + kTitleH + i * pitch;
         knob(ccs[i], colors[i], {x + kPad, ry, kCellW, kKnobH}, names[i]);
         indicator(ids[i], "Filter", {"VCF 1", "VCF 2"}, {x + kPad, ry + kKnobH, kCellW, 56}, vis::HwButton::Lights::Above,
                   kSteiner);
      }
      picture<vis::MixBar>({x + 12, y + h - kPad - 16, w - 24, 10}, ccs, colors);
   }

   // Mode and slope down the left, then drive, cutoff, resonance over brute factor, env amount, out.
   void buildFilter(float y, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list);
      const bool steiner = ccs[5] == 7;
      const float x = colX(1) + kMixerW + kGutter;
      section(title, {x, y, kFilterW, boxH(2)}, true, color);
      const std::string id = steiner ? "vcf1" : "vcf2";
      std::vector<std::string> modes = {"LP", "BP", "HP"};
      if (steiner)
         modes.push_back("NOTCH");
      indicator(id + ".mode", "Mode", std::move(modes), {x + kPad, y + kTitleH, kFilterButtonsW - 6, kKnobH},
                vis::HwButton::Lights::Right, color);
      indicator(id + ".slope", "Slope", {"24dB", "12dB"}, {x + kPad, y + kTitleH + kRowPitch, kFilterButtonsW - 6, kKnobH},
                vis::HwButton::Lights::Right, color);
      const char *names[6] = {"DRIVE", "CUTOFF", "RESONANCE", "BRUTE", "ENV AMT", steiner ? "STEINER OUT" : "LADDER OUT"};
      const float kx = x + kPad + kFilterButtonsW;
      for (int i = 0; i < 6; ++i)
         knob(ccs[i], color, {kx + (i % 3) * kCellW, y + kTitleH + (i / 3) * kRowPitch, kCellW, kKnobH}, names[i]);
   }

   // The routing between the filters: the routing switch and both responses on the left, the big
   // Master Cutoff knob on the right, as on the synth.
   void buildMasterCutoff() {
      const float x = colX(1) + kMixerW + kGutter, y = kTop + boxH(2) + kGutter, w = kFilterW, h = kCutoffH;
      add<vis::Tile>({x, y, w, h});
      indicator("vcf.routing", "Routing", {"PAR", "SER"}, {x + 10, y + 10, 110, 64}, vis::HwButton::Lights::Right);
      const float kw = 150, kh = 176;
      knob(27, kWhite, {x + w - kw - 6, y + (h - kh) * 0.5f, kw, kh}, "MASTER CUTOFF");
      picture<vis::DualFilter>({x + 12, y + 84, w - kw - 30, h - 96}, vis::DualFilter::Filter{23, 83, 24, kSteiner},
                               vis::DualFilter::Filter{25, 87, 26, kLadder});
   }

   // LFO 1 and LFO 2 side by side under the mixer and filters, mirror images as on the synth: the
   // phase or delay, sync and rate over the wave and retrigger.
   void buildLfo(int index, const char *title, unsigned color, int first, const char *firstName, int rate) {
      const float w = (kColW[1] - kGutter) / 2, x = colX(1) + index * (w + kGutter), y = kStripY, h = kStripH;
      section(title, {x, y, w, h}, false);
      const std::string id = index == 0 ? "lfo1" : "lfo2";
      const float ky = y + kTitleH + 12;
      knob(first, color, {x + kPad, ky, kCellW, kKnobH}, firstName);
      indicator(id + ".sync", "Seq-Sync", {}, {x + kPad + kCellW, ky + 10, w - 2 * kPad - 2 * kCellW, kKnobH - 20});
      knob(rate, color, {x + w - kPad - kCellW, ky, kCellW, kKnobH}, "RATE");
      leds_[index] = &rateLed({x + w - 20, y + 10}, color);
      const float by = ky + kKnobH + 10, bh = (y + h - kPad - by) / 2;
      indicator(id + ".wave", "< Wave >", {"SIN", "TRI", "SQR", "RSAW", "SAW", "RND", "S&H"}, {x + kPad, by, w - 2 * kPad, bh},
                vis::HwButton::Lights::Above, color);
      indicator(id + ".retrig", "Retrig", {"OFF", "SNGL", "MULTI"}, {x + kPad + 30, by + bh, w - 2 * kPad - 60, bh},
                vis::HwButton::Lights::Above, color);
   }

   ui::ActivityLed &rateLed(ui::Rect at, unsigned color) {
      auto &led = add<ui::ActivityLed>({at.x, at.y, 12, 12});
      led.setColor(color);
      led.fadeMs = 0;
      led.setLevel(1.0f);
      return led;
   }

   // ---- column 3: preset and envelopes ----

   // The synth's preset as far as the plugin knows, its preset buttons, and the read of every
   // preset stored on it.
   void buildPreset() {
      const float x = colX(2), y = kTop, w = kColW[2], h = kPresetH;
      section("PRESET", {x, y, w, h}, true);
      display_ = &add<vis::PresetDisplay>({x + (w - 156) * 0.5f, y + 28, 156, 48});
      const char *names[4] = {"Panel", "Save", "<", ">"};
      const char *ids[4] = {"preset.panel", "preset.save", "preset.prev", "preset.next"};
      const float bw = (w - 2 * kPad) / 4;
      for (int i = 0; i < 4; ++i)
         indicator(ids[i], names[i], {}, {x + kPad + i * bw, y + 82, bw, 44});
      place(readSynth_, {x + (w - 150) * 0.5f, y + 134, 150, 28});
      readSynth_.setAccent(kSteiner);
      readSynth_.onClick = [] { SynthLink::get().requestRead(); };
      tips_.add(readSynth_, "Reads every preset stored on the synth into the preset bar (about a minute). In Bitwig "
                            "this needs the MatrixBrute presets controller script (bitwig/MatrixBrute.control.js), "
                            "since a plugin there can't send or receive SysEx.");
      linkStatus_ = &label("", {x + kPad, y + 172, w - 2 * kPad, 18}, kDim, visage::Font::kCenter, 0.8f);
   }

   // The synth's envelopes are sliders: velocity (or ENV 3's delay) in white on the left, then
   // attack, decay, sustain and release, with the shape they make drawn above.
   void buildEnvelope(float y, float h, const char *title, int index, std::initializer_list<int> list, int velocity,
                      const char *velocityName) {
      const std::vector<int> st(list); // delay (or -1), attack, decay, sustain, release
      const float x = colX(2), w = kColW[2];
      const unsigned color = kEnvColors[index];
      section(title, {x, y, w, h}, false);
      const float shapeH = std::round(h * 0.26f);
      picture<vis::EnvShape>({x + kPad, y + kTitleH, w - 2 * kPad, shapeH}, st[0], st[1], st[2], st[3], st[4], color,
                             gate());
      const float cw = (w - 2 * kPad) / 5, top = y + kTitleH + shapeH + 12, sh = y + h - kPad - 48 - top;
      add<vis::Tile>({x + kPad - 2, top - 4, cw + 4, y + h - kPad - top + 4});
      const int first = velocity >= 0 ? velocity : st[0];
      vslider(first, kWhite, {x + kPad, 0, cw, 0}, top, sh, velocity >= 0 ? velocityName : "DELAY");
      const char *names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      for (int i = 0; i < 4; ++i)
         vslider(st[1 + i], color, {x + kPad + (i + 1) * cw, 0, cw, 0}, top, sh, names[i]);
   }

   // ---- column 4: the matrix ----

   // The matrix as printed on the synth. Its display shows what the plugin knows of the synth, with
   // the light for CCs heard from it beside, and how to read the panel underneath.
   void buildMatrix() {
      const float x = colX(3), y = kTop, w = kColW[3], h = kBandH + kGutter + kStripH;
      section("", {x, y, w, h}, true, 0xff4a525c);
      matrix_ = &add<vis::MatrixMap>({x + 10, y + 14, w - 20, h - 14 - 46});
      auto &led = add<ui::ActivityLed>({x + 16, y + 44, 120, 18}, "FROM SYNTH");
      led.setColor(kGreen);
      heardLed_ = &led;
      tips_.add(led, "Lights when a CC comes in on the MIDI channel: the synth's own knobs, with its MIDI out "
                     "routed into this track, or another controller.");
      tips_.add(*matrix_, "The matrix's routings are set on the synth: they aren't sent over MIDI. They're dim until "
                          "the plugin can read them from the synth's presets.");
      hint("A DIM KNOB IS A GUESS: NOT HEARD OR SET SINCE THE SYNTH'S LAST PRESET CHANGE. TURN IT ON THE SYNTH TO READ IT.",
           {x + 16, y + h - 40, w - 32, 12}, visage::Font::kLeft);
      hint("PICK ONE OF THE SYNTH'S PRESETS IN THE PRESET BAR TO LOAD IT; READ SYNTH FETCHES THE ONES STORED ON IT NOW.",
           {x + 16, y + h - 22, w - 32, 12}, visage::Font::kLeft);
   }

   // ---- column 5: analog effects, sequencer, and glide, play control and the macros ----

   // The echoes drawn on top, then delay time, regeneration, tone, width and dry/wet, over the sync
   // and mode buttons with the mode's lights.
   void buildFx() {
      const float x = colX(4), y = kTop, w = kColW[4], h = kFxH;
      section("ANALOG FX", {x, y, w, h}, true);
      const float buttonsH = 64, tapsH = h - kTitleH - kKnobH - buttonsH - 2 * kPad - 8;
      picture<vis::FxTaps>({x + 10, y + kTitleH + 2, w - 20, tapsH}, kCyan);
      const float ky = y + kTitleH + tapsH + 8;
      knobRow(x, ky - kTitleH, {112, 113, 114, 115, 31}, {"TIME", "REGEN", "TONE / RATE", "WIDTH", "DRY / WET"},
              {kCyan, kCyan, kCyan, kCyan, kWhite});
      const float by = ky + kKnobH + 4;
      indicator("fx.sync", "Sync", {}, {x + kPad, by, kCellW, buttonsH}, vis::HwButton::Lights::Above, kCyan);
      indicator("fx.mode", "Mode", {"STEREO DLY", "MONO DLY", "CHORUS", "FLANGER", "REVERB"},
                {x + kPad + kCellW, by, 4 * kCellW, buttonsH}, vis::HwButton::Lights::Above, kCyan);
   }

   // The sequencer's buttons as on the synth, with Rate, Swing and Gate down the right edge.
   void buildSequencer() {
      const float x = colX(4), y = kTop + kFxH + kGutter, w = kColW[4], h = kSeqH;
      section("SEQUENCER", {x, y, w, h}, true);
      // The tempo display (blank: the plugin doesn't know the synth's tempo) with Save and MIDI Sync,
      // then the rows of buttons.
      const float rh = (h - kTitleH - kPad) / 7;
      tempo_ = &add<vis::PresetDisplay>({x + kPad + 12, y + kTitleH + 8, 2 * kCellW, rh - 18});
      tempo_->setText("---");
      tips_.add(*tempo_, "The synth's tempo isn't sent over MIDI, so the display stays blank.");
      const char *rows[7][5] = {{nullptr, nullptr, nullptr, "Save", "MIDI Sync"},
                                {"Patterns", "Link", "SEQ Length", "Sequencer", "Arpeggiator"},
                                {"Note", "Triplet", "Dotted", nullptr, nullptr},
                                {"1/4", "1/8", "1/16", "1/32", nullptr},
                                {"Fwd", "Rev", "Fwd / Rev", "Rand", nullptr},
                                {"Step <", "Step >", "Tap", nullptr, nullptr},
                                {"Rec", "Play / Pause", "Play", nullptr, nullptr}};
      for (int r = 0; r < 7; ++r)
         for (int c = 0; c < 5; ++c)
            if (rows[r][c])
               indicator(std::string("seq.") + rows[r][c], rows[r][c], {},
                         {x + kPad + c * kCellW, y + kTitleH + r * rh, kCellW, rh - 4}, vis::HwButton::Lights::Above, kRed);
      const int ccs[3] = {116, 117, 118};
      const char *names[3] = {"RATE", "SWING", "GATE"};
      const float top = y + kTitleH + 2 * rh, kp = (y + h - kPad - top) / 3;
      for (int i = 0; i < 3; ++i)
         knob(ccs[i], kRed, {x + kPad + 4 * kCellW, top + i * kp + (kp - kKnobH) * 0.5f, kCellW, kKnobH}, names[i]);
   }

   // Glide, play control and the macros, from the synth's left end.
   void buildPerformance() {
      const float x = colX(4), y = kTop + kFxH + kSeqH + 2 * kGutter, w = kColW[4], h = kPerfH;
      section("GLIDE / PLAY CONTROL / MACROS", {x, y, w, h}, false);
      knobRow(x, y, {5, 11, 2, 4, 3}, {"GLIDE", "M1", "M2", "M3 / EXP 1", "M4 / EXP 2"}, {kWhite});
      const float by = y + kTitleH + kKnobH + 4, bh = h - (by - y) - kPad;
      indicator("glide.on", "Glide On/Off", {}, {x + kPad, by, kCellW, bh});
      indicator("play.keyHold", "Key Hold", {}, {x + kPad + kCellW, by, kCellW, bh});
      indicator("play.priority", "Note Priority", {"LOW", "HIGH", "LAST"}, {x + kPad + 2 * kCellW, by, kCellW, bh});
      indicator("play.legato", "Legato", {"ON", "OFF", "GLIDE"}, {x + kPad + 3 * kCellW, by, kCellW, bh});
      indicator("play.octave", "Octave", {"-2", "-1", "0", "+1", "+2"}, {x + kPad + 4 * kCellW, by, kCellW, bh});
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
      matrix_->setDisplay({"MATRIXBRUTE CC", std::to_string(known) + " OF " + std::to_string(kCcParamCount) + " KNOBS KNOWN",
                           program < 0 ? "NO SYNTH PRESET PICKED OR HEARD YET" : "THE SYNTH IS ON PRESET " + code,
                           "BUTTONS AND MATRIX: NOT READ YET"});
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
   ui::Label *wheelValue_ = nullptr;
   vis::PresetDisplay *display_ = nullptr;
   vis::MatrixMap *matrix_ = nullptr;
   vis::PresetDisplay *tempo_ = nullptr;
   // The synth's buttons that aren't on MIDI, by id ("vcf1.mode", "lfo2.wave"...), for setIndicator().
   std::vector<std::pair<std::string, vis::HwButton *>> indicators_;
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
