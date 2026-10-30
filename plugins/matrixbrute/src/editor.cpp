// Visage panel for the MatrixBrute controller, laid out on a fixed kDesignWidth x kDesignHeight grid
// and scaled to the window, which keeps its aspect ratio.
//
// It follows the synth's own panel, column by column, with every box sized to its contents on one
// knob pitch: VCO 1 and 2 (the wave mix drawn under them), VCO 3 and noise, over audio mod, VCO
// sync, the wheels and voice; the mixer (each row its level, Filter button, lights and mix share) beside the
// Steiner and ladder filters, with the routing and the big silver Master Cutoff between them, over
// LFO 1 and LFO 2; the preset display over the three envelopes; the 16 x 16 mod matrix with its
// display, mode buttons and sequencer lanes; and master, the analog effects, the sequencer and, from
// the synth's left end, glide, play control and the macros. As on the synth, a dark upper band sits
// over a lighter strip, and box edges line up across columns (ENV 2 with the sequencer, ENV 3 with
// VCO 3, the strip's two rows). Each box's title tab and top rule are in its accent, from about
// eight hues kept apart, and its controls are named in a light tint of it (the mixer's and audio
// mod's in each source's colour); values and CC numbers stay neutral. Names are at least 13.5 units
// and the smallest text 12. Every knob and slider is one CC. The synth's buttons, lights and matrix
// aren't on MIDI: they're drawn as indicators with a state the preset read can set later
// (setIndicator() and MatrixMap::setCell()), and until then they're dim.
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

constexpr float kDesignHeight = 1488.0f;
constexpr float kHeaderHeight = 64.0f;
// The window opens at this fraction of the design size; right-click the background for others.
constexpr float kOpenScale = 0.5f;

// One knob pitch everywhere: a cell is kCellW wide and kKnobH tall, rows kRowPitch apart, and a box
// holding n cells across and r rows down is boxW(n) x boxH(r).
constexpr float kCellW = 92, kKnobH = 100, kRowPitch = 104;
constexpr float kPad = 8, kTitleH = 24;
constexpr float boxW(int n) { return 2 * kPad + n * kCellW; }
constexpr float boxH(int r) { return kTitleH + r * kRowPitch - (kRowPitch - kKnobH) + kPad; }
// Type sizes: control names, values and the smallest text (CC numbers, light names).
constexpr float kNameSize = vis::kNameText, kValueSize = 13.0f, kSmallSize = vis::kSmallText;

// The synth's columns, left to right, with 16 point margins and 12 point gutters: oscillators,
// mixer and filters, preset and envelopes, the matrix, and effects and sequencer.
constexpr float kMargin = 16.0f, kGutter = 12.0f;
// The mixer's rows run across as on the synth: the level knob, then the Filter button with its lights.
constexpr float kMixerButtonW = 100, kMixerW = 2 * kPad + kCellW + kMixerButtonW;
constexpr float kFilterButtonsW = 110, kFilterW = boxW(3) + kFilterButtonsW;
// The matrix's 16 columns are 46 wide, between the source names (142) and the sequencer lanes (58),
// in blocks of four 10 apart, inside a 10 point border.
constexpr float kMatrixW = 20 + 142 + 58 + 3 * 10 + 16 * 46;
constexpr float kColW[] = {boxW(4) + 136, kMixerW + kGutter + kFilterW, 310, kMatrixW, boxW(5)};
constexpr float colX(int column) {
   float x = kMargin;
   for (int i = 0; i < column; ++i)
      x += kColW[i] + kGutter;
   return x;
}
constexpr float kDesignWidth = colX(4) + kColW[4] + kMargin;
// A dark band over a lighter strip. The band is as tall as two VCOs (their waves under the knobs)
// and VCO 3's row; the strip is two rows of one-knob-row boxes on one rhythm: audio mod and VCO sync
// over the wheels and voice, and each LFO's knobs over its wave and retrigger.
constexpr float kTop = kHeaderHeight + kMargin;
constexpr float kWaveH = 224;
constexpr float kVcoH = boxH(2) + kWaveH + 14, kSmallH = boxH(1);
constexpr float kBandH = 2 * kVcoH + kSmallH + 2 * kGutter;
constexpr float kStripRowH = kSmallH, kStripH = 2 * kStripRowH + kGutter, kStripY = kTop + kBandH + kGutter;
constexpr float kStripRow2Y = kStripY + kStripRowH + kGutter;
static_assert(kStripY + kStripH + kMargin == kDesignHeight, "the band and strip fill the height");
constexpr float kVco2Y = kTop + kVcoH + kGutter, kVco3Y = kVco2Y + kVcoH + kGutter;
// The filters at the top and bottom of the band, with the routing and Master Cutoff between.
constexpr float kFilterH = 300, kCutoffH = kBandH - 2 * kFilterH - 2 * kGutter;
// The preset at the top of its column, then the envelopes: ENV 3 starts level with VCO 3's row and
// runs to the foot, ENV 1 and ENV 2 share what's between.
constexpr float kPresetH = 166;
constexpr float kEnv1Y = kTop + kPresetH + kGutter, kEnv3Y = kVco3Y;
constexpr float kEnvH = (kEnv3Y - kEnv1Y - 2 * kGutter) / 2, kEnv2Y = kEnv1Y + kEnvH + kGutter;
constexpr float kEnv3H = kDesignHeight - kMargin - kEnv3Y;
// The last column: master, the analog effects down to ENV 2's top, the sequencer level with ENV 2
// down to the strip, and glide, play control and the macros across the strip.
constexpr float kMasterH = 92;
constexpr float kFxY = kTop + kMasterH + kGutter, kSeqY = kEnv2Y;
constexpr float kFxH = kSeqY - kGutter - kFxY, kSeqH = kStripY - kGutter - kSeqY;

// Section accents, about eight hues kept apart: the filters' orange and teal as printed on the
// synth, the envelopes' yellow, green and purple, VCO 1 amber, VCO 2 blue, VCO 3 coral, LFO 1 lime
// and LFO 2 pink, the matrix's blue, an azure for the effects and red for the sequencer. The boxes
// that route or hold the rest (mixer, audio mod, routing, preset, master) are silver, the wheels
// and voice grey.
constexpr unsigned kAmber = 0xfff2c94c;
constexpr unsigned kBlue = 0xff72abf2;
constexpr unsigned kCoral = 0xffff806e;
constexpr unsigned kGreen = 0xff9bd36a;
constexpr unsigned kRed = 0xffff5b5b;
constexpr unsigned kNoise = 0xffc9cdd3;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kDim = 0xff8d939c;
constexpr unsigned kHint = 0xff5d646e;
constexpr unsigned kUnknown = 0xff4b515a;
constexpr unsigned kLime = 0xffc2dc58;
constexpr unsigned kPink = 0xffff6fb5;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kAzure = 0xff5aa8ff;
constexpr unsigned kGrey = 0xffa8afb8;
constexpr unsigned kWheel = 0xffd9dde2;
// The filters' own colours, as printed on the synth: orange for the Steiner, teal for the ladder.
constexpr unsigned kSteiner = 0xfff07a3a;
constexpr unsigned kLadder = 0xff2fd1c5;
// ENV 1 is yellow rather than the Steiner's orange it feeds, so it stands apart from the filter beside it.
constexpr unsigned kEnv1 = 0xffffd45a;
constexpr unsigned kEnvColors[] = {kEnv1, kGreen, kPurple};
constexpr unsigned kSilver = 0xffc9d0d8;
constexpr unsigned kMatrixBlue = 0xff6f8cff;

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
      buildFilter(kTop + kBandH - kFilterH, "VCF2 LADDER", kLadder, {85, 25, 87, 86, 26, 8});
      buildLfo(0, "LFO 1", kLime, 90, "PHASE", 91);
      buildLfo(1, "LFO 2", kPink, 92, "DELAY", 93);
      buildPreset();
      buildEnvelope(kEnv1Y, kEnvH, "ENV 1 (VCF)", 0, {-1, 102, 103, 28, 104}, 94, "VELO/VCF");
      buildEnvelope(kEnv2Y, kEnvH, "ENV 2 (VCA)", 1, {-1, 105, 106, 29, 107}, 95, "VELO/VCA");
      buildEnvelope(kEnv3Y, kEnv3H, "ENV 3", 2, {108, 109, 110, 30, 111}, -1, "");
      buildMatrix();
      buildMaster();
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

   // A knob for a CC, labelled as on the synth. Its name is in the section's tint, or `labelColor`.
   ui::Knob &knob(int cc, unsigned accent, Rect cell, const char *label, unsigned labelColor = 0) {
      auto &k = add<ui::Knob>(cell, rangeOf(cc));
      k.setLabel(label);
      k.setLabelColor(labelColor ? labelColor : tint_);
      k.setTextSizes(kNameSize, kValueSize, kSmallSize);
      k.setCaption("CC " + std::to_string(cc));
      wire(k, cc, accent);
      return k;
   }
   // A vertical slider in a column `cell` wide: its name above, its value and CC number below.
   // The first slider's name starts at its tile's left edge, so a long one keeps clear of the next.
   ui::Slider &vslider(int cc, unsigned accent, Rect cell, float top, float height, const char *name, bool first = false) {
      if (first)
         text(name, cell.x + 1, top, cell.w + 16, kSmallSize, tint_, visage::Font::kLeft);
      else
         text(name, cell.x - 8, top, cell.w + 16, kSmallSize, tint_);
      auto &s = add<ui::Slider>(Rect{cell.x + cell.w * 0.2f, top + 20, cell.w * 0.6f, height},
                                ui::Slider::Orientation::Vertical, rangeOf(cc));
      wire(s, cc, accent);
      s.valueText = [](double) { return std::string(); };
      readouts_.push_back({&text("", cell.x - 6, top + height + 24, cell.w + 12, kValueSize, kWhite), cc});
      text("CC " + std::to_string(cc), cell.x - 6, top + height + 42, cell.w + 12, kSmallSize, kDim);
      return s;
   }
   // The room a slider's name, value and CC number take besides the slider.
   static constexpr float kSliderExtraH = 60;
   ui::Label &label(const std::string &str, Rect r, unsigned color = kWhite,
                    visage::Font::Justification j = visage::Font::kLeft, float fontScale = 0.78f) {
      return add<ui::Label>(r, str, color, j, fontScale);
   }
   // A line of text `size` units tall at x, y.
   ui::Label &text(const std::string &str, float x, float y, float w, float size, unsigned color,
                   visage::Font::Justification j = visage::Font::kCenter) {
      return add<ui::Label>(Rect{x, y, w, size / 0.8f}, str, color, j, 0.8f);
   }
   // A box with its title tab and top rule in `accent`; the controls built after it, until the
   // next box, take the accent's tint for their names.
   void section(const std::string &title, Rect r, bool dark, unsigned accent) {
      add<vis::HwSection>(r, title, dark, accent);
      tint_ = vis::labelTint(accent);
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
      b.setLabelColor(tint_);
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
      section(title, {x, y, w, h}, true, color);
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

   // VCO 3's coarse tune, wave, LFO divider and keyboard tracking; then the noise type. The lights
   // stand in a column beside each button, so their names keep a readable size.
   void buildVco3AndNoise() {
      const float x = colX(0), y = kVco3Y, w = 400, h = kSmallH;
      section("VCO 3 - LFO 3", {x, y, w, h}, true, kCoral);
      knobRow(x, y, {22}, {"COARSE"}, {kCoral});
      const float bx = x + kPad + kCellW, by = y + kTitleH + 4, bh = kKnobH - 8;
      const auto right = vis::HwButton::Lights::Right;
      indicator("vco3.wave", "Wave", {"RSAW", "SQR", "TRI", "SIN"}, {bx + 4, by, 100, bh}, right, kCoral);
      indicator("vco3.lfoDiv", "LFO Div", {"16", "32", "64", "128"}, {bx + 112, by, 90, bh}, right, kCoral);
      indicator("vco3.kbdTrack", "Kbd Track", {}, {bx + 208, by, x + w - kPad - (bx + 208), bh});
      const float nx = x + w + kGutter, nw = kColW[0] - w - kGutter;
      section("NOISE", {nx, y, nw, h}, true, kNoise);
      indicator("noise.type", "Type", {"WHT", "PNK", "RED", "BLU"}, {nx + kPad, by, nw - 2 * kPad, bh}, right);
   }

   // The four audio mod amounts, each named in its source's colour, and VCO sync in a small box of
   // its own beside them.
   void buildAudioModAndSync() {
      const float x = colX(0), y = kStripY, sw = 120, w = kColW[0] - sw - kGutter;
      section("AUDIO MOD", {x, y, w, kStripRowH}, false, kSilver);
      const int ccs[4] = {77, 78, 79, 80};
      const char *names[4] = {"VCO1 > VCO2", "VCO3 > 1 | 2", "VCO3 > F1 | F2", "NOISE > 1 | F1"};
      const unsigned colors[4] = {kAmber, kCoral, kCoral, kNoise};
      const float cw = (w - 2 * kPad) / 4;
      for (int i = 0; i < 4; ++i)
         knob(ccs[i], colors[i], {x + kPad + i * cw, y + kTitleH, cw, kKnobH}, names[i], vis::labelTint(colors[i]));
      const float sx = x + w + kGutter;
      section("VCO SYNC", {sx, y, sw, kStripRowH}, false, kSilver);
      indicator("vco.sync", "VCO 2 > VCO 1", {}, {sx + kPad, y + kTitleH + 14, sw - 2 * kPad, 64});
   }

   // Bend range, the mod wheel's destination and the mod wheel itself; then the voice mode in a
   // small box of its own.
   void buildWheelsAndVoice() {
      const float x = colX(0), y = kStripRow2Y, vw = 136, w = kColW[0] - vw - kGutter;
      section("WHEELS", {x, y, w, kStripRowH}, false, kGrey);
      knobRow(x, y, {119}, {"BEND RANGE"}, {kWhite});
      const float by = y + kTitleH + 4, bh = kKnobH - 8;
      indicator("wheel.mod", "Wheel Dest", {"MATRIX", "CUTOFF", "LFO1 VIB", "LFO1 AMT"}, {x + kPad + kCellW + 4, by, 136, bh},
                vis::HwButton::Lights::Right);
      const float wx = x + kPad + kCellW + 146;
      auto &wheel = add<vis::WheelStrip>({wx, y + kTitleH + 2, 24, kKnobH - 6}, rangeOf(1));
      wire(wheel, 1, kWheel);
      const float lx = wx + 30, lw = x + w - 4 - lx;
      text("MOD WHEEL", lx, y + 40, lw, 13, tint_, visage::Font::kLeft);
      wheelValue_ = &text("", lx, y + 62, lw, kValueSize, kWhite, visage::Font::kLeft);
      text("CC 1", lx, y + 82, lw, kSmallSize, kDim, visage::Font::kLeft);
      const float vx = x + w + kGutter;
      section("VOICE", {vx, y, vw, kStripRowH}, false, kGrey);
      indicator("voice.mode", "Mode", {"MONO", "PARA", "DUO"}, {vx + kPad, by, vw - 2 * kPad, bh},
                vis::HwButton::Lights::Right);
   }

   // ---- column 2: mixer, filters, routing and Master Cutoff, over the LFOs ----

   // Five rows as on the synth, each the level knob then the Filter button with its two lights,
   // named in the source's colour; the mix drawn at the foot.
   void buildMixer() {
      const float x = colX(1), y = kTop, w = kMixerW, h = kBandH;
      section("MIXER", {x, y, w, h}, true, kSilver);
      const int ccs[5] = {18, 19, 20, 21, 89};
      const char *names[5] = {"VCO 1", "VCO 2", "VCO 3", "NOISE", "EXTERNAL"};
      const char *ids[5] = {"mixer.vco1.filter", "mixer.vco2.filter", "mixer.vco3.filter", "mixer.noise.filter",
                            "mixer.ext.filter"};
      const unsigned colors[5] = {kAmber, kBlue, kCoral, kNoise, kWhite};
      // Each row is a tile filling its share of the height: the knob and Filter button, then the
      // source's level as a meter with its share of the mix.
      const float mixH = 44, top = y + kTitleH + 6, gap = 10;
      const float tileH = (y + h - kPad - mixH - top - 4 * gap) / 5, meterH = 34;
      for (int i = 0; i < 5; ++i) {
         const float ty = top + i * (tileH + gap), ry = ty + (tileH - kKnobH - meterH - 6) * 0.5f;
         add<vis::Tile>({x + kPad - 2, ty, w - 2 * kPad + 4, tileH});
         const unsigned tint = vis::labelTint(colors[i]);
         knob(ccs[i], colors[i], {x + kPad, ry, kCellW, kKnobH}, names[i], tint);
         indicator(ids[i], "Filter", {"VCF 1", "VCF 2"}, {x + kPad + kCellW, ry + 14, kMixerButtonW, kKnobH - 28},
                   vis::HwButton::Lights::Right, kSteiner)
            .setLabelColor(tint);
         picture<vis::MixMeter>({x + 16, ry + kKnobH + 6, w - 32, meterH}, ccs, i, colors[i]);
      }
      const float my = y + h - kPad - mixH;
      text("MIX", x + 14, my + 14, 40, kSmallSize, kDim, visage::Font::kLeft);
      picture<vis::MixBar>({x + 54, my + 18, w - 68, 10}, ccs, colors);
   }

   // Mode and slope down the left, then drive, cutoff, resonance over brute factor, env amount, out.
   void buildFilter(float y, const char *title, unsigned color, std::initializer_list<int> list) {
      const std::vector<int> ccs(list);
      const bool steiner = ccs[5] == 7;
      const float x = colX(1) + kMixerW + kGutter, h = kFilterH;
      section(title, {x, y, kFilterW, h}, true, color);
      // The two rows, with the box's spare height shared out above, between and below them.
      const float gap = (h - kTitleH - kPad - 2 * kKnobH) / 3, row1 = y + kTitleH + gap, row2 = row1 + kKnobH + gap;
      const std::string id = steiner ? "vcf1" : "vcf2";
      std::vector<std::string> modes = {"LP", "BP", "HP"};
      if (steiner)
         modes.push_back("NOTCH");
      indicator(id + ".mode", "Mode", std::move(modes), {x + kPad, row1, kFilterButtonsW - 6, kKnobH},
                vis::HwButton::Lights::Right, color);
      indicator(id + ".slope", "Slope", {"24dB", "12dB"}, {x + kPad, row2, kFilterButtonsW - 6, kKnobH},
                vis::HwButton::Lights::Right, color);
      const char *names[6] = {"DRIVE", "CUTOFF", "RESONANCE", "BRUTE", "ENV AMT", steiner ? "STEINER OUT" : "LADDER OUT"};
      const float kx = x + kPad + kFilterButtonsW;
      for (int i = 0; i < 6; ++i)
         knob(ccs[i], color, {kx + (i % 3) * kCellW, i < 3 ? row1 : row2, kCellW, kKnobH}, names[i]);
   }

   // The routing between the filters: both responses drawn across the top, then the routing switch
   // beside the big silver Master Cutoff knob in its ring, as on the synth.
   void buildMasterCutoff() {
      const float x = colX(1) + kMixerW + kGutter, y = kTop + kFilterH + kGutter, w = kFilterW, h = kCutoffH;
      section("ROUTING / MASTER CUTOFF", {x, y, w, h}, true, kSilver);
      // The knob's cell: no name (the box has it), the ring 86 across, its centre 79 from the top.
      const float kw = 150, kh = 180, ky = y + h - kh - 4, ringY = ky + 79, ring = 86 + 40;
      picture<vis::DualFilter>({x + 12, y + kTitleH + 4, w - 24, ky - y - kTitleH - 16}, vis::DualFilter::Filter{23, 83, 24, kSteiner},
                               vis::DualFilter::Filter{25, 87, 26, kLadder});
      const float groupW = 130 + 36 + ring, gx = x + (w - groupW) * 0.5f;
      indicator("vcf.routing", "Routing", {"PAR", "SER"}, {gx, ringY - 32, 130, 64}, vis::HwButton::Lights::Right);
      const float kcx = gx + 130 + 36 + ring * 0.5f;
      add<vis::Bezel>({kcx - ring * 0.5f, ringY - ring * 0.5f, ring, ring});
      auto &cutoff = knob(27, kSilver, {kcx - kw * 0.5f, ky, kw, kh}, "");
      cutoff.setCapColors(0xffe8ebee, 0xff858d96, 0xff1a1d22);
   }

   // LFO 1 and LFO 2 side by side under the mixer and filters, mirror images as on the synth: the
   // phase or delay, sync and rate, then right under them the wave's lights in a row over its
   // button, and retrigger with its lights beside it.
   void buildLfo(int index, const char *title, unsigned color, int first, const char *firstName, int rate) {
      const float w = (kColW[1] - kGutter) / 2, x = colX(1) + index * (w + kGutter), y = kStripY, h = kStripH;
      section(title, {x, y, w, h}, false, color);
      const std::string id = index == 0 ? "lfo1" : "lfo2";
      const float ky = y + kTitleH;
      knob(first, color, {x + kPad, ky, kCellW, kKnobH}, firstName);
      indicator(id + ".sync", "Seq-Sync", {}, {x + kPad + kCellW, ky + 16, w - 2 * kPad - 2 * kCellW, 60});
      knob(rate, color, {x + w - kPad - kCellW, ky, kCellW, kKnobH}, "RATE");
      leds_[index] = &rateLed({x + w - 20, y + 10}, color);
      const float by = ky + kKnobH + 22, rw = 100, ww = w - 2 * kPad - rw - 6;
      indicator(id + ".wave", "< Wave >", {"SIN", "TRI", "SQR", "RSAW", "SAW", "RND", "S&H"}, {x + kPad, by, ww, 96},
                vis::HwButton::Lights::Above, color);
      indicator(id + ".retrig", "Retrig", {"OFF", "SNGL", "MULTI"}, {x + w - kPad - rw, by + 22, rw, 70},
                vis::HwButton::Lights::Right, color);
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
      section("PRESET", {x, y, w, h}, true, kSilver);
      display_ = &add<vis::PresetDisplay>({x + (w - 150) * 0.5f, y + 26, 150, 40});
      tips_.add(*display_, "The synth's preset: the one picked in the preset bar, or the last Program Change heard. "
                           "Every knob is a guess (drawn dim) until it's heard or set after a preset change.");
      const char *names[4] = {"Panel", "Save", "<", ">"};
      const char *ids[4] = {"preset.panel", "preset.save", "preset.prev", "preset.next"};
      const float bw = (w - 2 * kPad) / 4;
      for (int i = 0; i < 4; ++i)
         indicator(ids[i], names[i], {}, {x + kPad + i * bw, y + 70, bw, 38});
      place(readSynth_, {x + (w - 150) * 0.5f, y + 112, 150, 26});
      readSynth_.setAccent(kSteiner);
      readSynth_.onClick = [] { SynthLink::get().requestRead(); };
      tips_.add(readSynth_, "Reads every preset stored on the synth into the preset bar (about a minute). In Bitwig "
                            "this needs the MatrixBrute presets controller script (bitwig/MatrixBrute.control.js), "
                            "since a plugin there can't send or receive SysEx.");
      linkStatus_ = &text("", x + kPad, y + 142, w - 2 * kPad, kSmallSize, kDim);
   }

   // The synth's envelopes are sliders: velocity (or ENV 3's delay) on a darker tile on the left,
   // then attack, decay, sustain and release, with the shape they make drawn above.
   void buildEnvelope(float y, float h, const char *title, int index, std::initializer_list<int> list, int velocity,
                      const char *velocityName) {
      const std::vector<int> st(list); // delay (or -1), attack, decay, sustain, release
      const float x = colX(2), w = kColW[2];
      const unsigned color = kEnvColors[index];
      section(title, {x, y, w, h}, false, color);
      const float shapeH = std::round(h * 0.39f);
      picture<vis::EnvShape>({x + kPad, y + kTitleH, w - 2 * kPad, shapeH}, st[0], st[1], st[2], st[3], st[4], color,
                             gate());
      const float cw = (w - 2 * kPad) / 5, top = y + kTitleH + shapeH + 10, sh = y + h - kPad - kSliderExtraH - top;
      add<vis::Tile>({x + kPad - 2, top - 4, cw + 4, y + h - kPad - top + 4});
      const int first = velocity >= 0 ? velocity : st[0];
      vslider(first, kWhite, {x + kPad, 0, cw, 0}, top, sh, velocity >= 0 ? velocityName : "DELAY", true);
      const char *names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      for (int i = 0; i < 4; ++i)
         vslider(st[1 + i], color, {x + kPad + (i + 1) * cw, 0, cw, 0}, top, sh, names[i]);
   }

   // ---- column 4: the matrix ----

   // The matrix as printed on the synth. Its display shows what the plugin knows of the synth and
   // how to read the panel.
   void buildMatrix() {
      const float x = colX(3), y = kTop, w = kColW[3], h = kBandH + kGutter + kStripH;
      section("MOD MATRIX", {x, y, w, h}, true, kMatrixBlue);
      matrix_ = &add<vis::MatrixMap>({x + 10, y + 26, w - 20, h - 26 - 10}, tint_);
      tips_.add(*matrix_, "The matrix's routings are set on the synth: they aren't sent over MIDI. They're dim until "
                          "the plugin can read them from the synth's presets.");
   }

   // ---- column 5: master, analog effects, sequencer, glide, play control and the macros ----

   // Fine tune, phones and master volume as on the synth (not on MIDI), with the light for CCs heard
   // from the synth between them.
   void buildMaster() {
      const float x = colX(4), y = kTop, w = kColW[4], kw = 112, ky = y + kTitleH + 2, kh = kMasterH - kTitleH - 6;
      section("MASTER", {x, y, w, kMasterH}, true, kSilver);
      const char *names[3] = {"FINE TUNE", "PHONES", "VOLUME"};
      const float xs[3] = {x + kPad, x + w - kPad - 2 * kw, x + w - kPad - kw}, ws[3] = {kw + 14, kw, kw};
      for (int i = 0; i < 3; ++i) {
         auto &k = add<vis::HwKnob>({xs[i], ky, ws[i], kh}, names[i], tint_, i == 2);
         tips_.add(k, std::string(names[i]) + " is a knob on the synth that isn't sent over MIDI.");
      }
      const float lx = xs[0] + ws[0] + 6;
      auto &led = add<ui::ActivityLed>({lx, ky + kh * 0.5f - 11, xs[1] - 4 - lx, 22}, "FROM SYNTH");
      led.setColor(kGreen);
      heardLed_ = &led;
      tips_.add(led, "Lights when a CC comes in on the MIDI channel: the synth's own knobs, with its MIDI out "
                     "routed into this track, or another controller.");
   }

   // As on the synth: the echoes drawn on top, then delay time, regeneration, tone and width; under
   // them sync, mode with its five lights in a column, and dry/wet.
   void buildFx() {
      const float x = colX(4), y = kFxY, w = kColW[4], h = kFxH, cw = (w - 2 * kPad) / 4;
      section("ANALOG FX", {x, y, w, h}, true, kAzure);
      const float tapsH = 120, rows = y + kTitleH + 6 + tapsH;
      picture<vis::FxTaps>({x + 10, y + kTitleH + 6, w - 20, tapsH}, kAzure);
      const float gap = (y + h - kPad - rows - 2 * kKnobH) / 3, row1 = rows + gap, row2 = row1 + kKnobH + gap;
      const int ccs[4] = {112, 113, 114, 115};
      const char *names[4] = {"DELAY TIME", "REGEN", "TONE / RATE", "WIDTH / DEPTH"};
      for (int i = 0; i < 4; ++i)
         knob(ccs[i], kAzure, {x + kPad + i * cw, row1, cw, kKnobH}, names[i]);
      indicator("fx.sync", "Sync", {}, {x + kPad, row2 + 14, cw, 64}, vis::HwButton::Lights::Above, kAzure);
      const float modeW = 160;
      indicator("fx.mode", "Mode", {"STEREO DLY", "MONO DLY", "CHORUS", "FLANGER", "REVERB"}, {x + kPad + 2 * cw - modeW * 0.5f, row2, modeW, kKnobH},
                vis::HwButton::Lights::Right, kAzure);
      knob(31, kWhite, {x + kPad + 3 * cw, row2, cw, kKnobH}, "DRY / WET");
   }

   // The sequencer's buttons as on the synth: the tempo display with Save and MIDI Sync, the five
   // mode buttons, then the grid of note lengths, rates, directions, step and transport with Rate,
   // Swing and Gate down the right edge.
   void buildSequencer() {
      const float x = colX(4), y = kSeqY, w = kColW[4], h = kSeqH;
      section("SEQUENCER", {x, y, w, h}, true, kRed);
      const float ty = y + kTitleH, rowH = 52;
      tempo_ = &add<vis::PresetDisplay>({x + kPad + 14, ty + 6, 200, rowH - 12}, "888.8");
      tempo_->setText("----");
      tips_.add(*tempo_, "The synth's tempo isn't sent over MIDI, so the display stays blank.");
      struct Cell {
         const char *id, *name;
      };
      const auto button = [&](Cell cell, float bx, float by, float bw, float bh) {
         indicator(std::string("seq.") + cell.id, cell.name, {}, {bx, by, bw, bh}, vis::HwButton::Lights::Above, kRed);
      };
      const float cx = x + kPad;
      button({"save", "Save"}, cx + 3 * kCellW, ty, kCellW, rowH);
      button({"midiSync", "MIDI Sync"}, cx + 4 * kCellW, ty, kCellW, rowH);
      const Cell modes[5] = {{"patterns", "Patterns"}, {"link", "Link"}, {"length", "SEQ Length"}, {"sequencer", "Sequencer"},
                             {"arpeggiator", "Arpeggiator"}};
      for (int c = 0; c < 5; ++c)
         button(modes[c], cx + c * kCellW, ty + rowH, kCellW, rowH);
      // The grid, on four columns. As on the synth the note lengths are three buttons grouped on a
      // tile, Tap sits at the right of the step row, and the transport's three buttons share the width.
      const float top = ty + 2 * rowH + 10, rh = (y + h - kPad - top) / 5;
      add<vis::Tile>({cx + 2, top + 2, 3 * kCellW - 4, rh - 4});
      const Cell notes[3] = {{"note", "Note"}, {"triplet", "Trip"}, {"dotted", "Dot"}};
      for (int c = 0; c < 3; ++c)
         button(notes[c], cx + c * kCellW, top, kCellW, rh);
      const Cell rates[4] = {{"1/4", "1/4"}, {"1/8", "1/8"}, {"1/16", "1/16"}, {"1/32", "1/32"}};
      const Cell dirs[4] = {{"fwd", "Fwd"}, {"rev", "Rev"}, {"fwdRev", "Fwd/Rev"}, {"rand", "Rand"}};
      for (int c = 0; c < 4; ++c) {
         button(rates[c], cx + c * kCellW, top + rh, kCellW, rh);
         button(dirs[c], cx + c * kCellW, top + 2 * rh, kCellW, rh);
      }
      button({"stepBack", "Step <"}, cx, top + 3 * rh, kCellW, rh);
      button({"stepNext", "Step >"}, cx + kCellW, top + 3 * rh, kCellW, rh);
      button({"tap", "Tap"}, cx + 3 * kCellW, top + 3 * rh, kCellW, rh);
      const Cell transport[3] = {{"rec", "Rec"}, {"playPause", "Pause"}, {"play", "Play"}};
      for (int c = 0; c < 3; ++c)
         button(transport[c], cx + c * kCellW * 4 / 3, top + 4 * rh, kCellW * 4 / 3, rh);
      const int ccs[3] = {116, 117, 118};
      const char *names[3] = {"RATE", "SWING", "GATE"};
      const float kpitch = (5 * rh - kKnobH) / 2;
      for (int i = 0; i < 3; ++i) {
         auto &k = knob(ccs[i], kRed, {cx + 4 * kCellW, top + i * kpitch, kCellW, kKnobH}, names[i]);
         if (i == 0)
            k.setCapColors(0xffe8ebee, 0xff9aa1a9, 0xff1a1d22); // the synth's white Rate knob
      }
   }

   // Glide, play control and the macros, from the synth's left end: the knobs over their buttons.
   void buildPerformance() {
      const float x = colX(4), y = kStripY, w = kColW[4], h = kStripH;
      section("GLIDE / PLAY CONTROL / MACROS", {x, y, w, h}, false, kLime);
      const int ccs[5] = {5, 11, 2, 4, 3};
      const char *names[5] = {"GLIDE", "M1", "M2", "M3 / EXP 1", "M4 / EXP 2"};
      for (int i = 0; i < 5; ++i) {
         auto &k = knob(ccs[i], i == 0 ? kLime : kWhite, {x + kPad + i * kCellW, y + kTitleH, kCellW, kKnobH}, names[i]);
         if (i > 0)
            k.setCapColors(0xffc3c8ce, 0xff6f767e, 0xff1a1d22); // the macros' light grey caps
      }
      const float by = y + kTitleH + kKnobH + 20, bh = h - (by - y) - kPad;
      // Note Priority's and Legato's lights get the room their names need, at the same size as Octave's.
      const float ws[5] = {70, 74, 104, 114, w - 2 * kPad - 362};
      const char *ids[5] = {"glide.on", "play.keyHold", "play.priority", "play.legato", "play.octave"};
      const char *labels[5] = {"Glide On/Off", "Key Hold", "Note Priority", "Legato", "Octave"};
      const std::vector<std::string> lights[5] = {{}, {}, {"LOW", "HIGH", "LAST"}, {"ON", "OFF", "GLIDE"}, {"-2", "-1", "0", "+1", "+2"}};
      float bx = x + kPad;
      for (int i = 0; i < 5; ++i) {
         indicator(ids[i], labels[i], lights[i], {bx, by, ws[i], bh});
         bx += ws[i];
      }
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
                           "BUTTONS AND MATRIX: NOT READ YET", "",
                           "A DIM KNOB IS A GUESS: TURN IT ON THE SYNTH",
                           "PICK A PRESET IN THE BAR TO LOAD IT"});
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
   // The names' colour for the box being built.
   unsigned tint_ = kWhite;
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
