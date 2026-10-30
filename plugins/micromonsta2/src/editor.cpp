// Visage panel for the MicroMonsta 2 controller, laid out on a fixed kDesignWidth x kDesignHeight
// grid and scaled to the window, which keeps its aspect ratio.
//
// Three pages, picked by the tabs in the header:
//
//    SOUND          the audio path (sources, filter, voice, effects, out), the envelopes, and the
//                   voice, effect and program modes;
//    MODULATION     the LFOs and performance controls, the mod matrix and its operators;
//    ARP & PROGRAM  the arpeggiator, what the synth's own knobs do, and the program's tempo,
//                   volume and category.
//
// Every knob is one CC; the dropdowns, toggles and number boxes are the settings no CC reaches, sent
// as NRPN (settings.h). The pictures in visuals.h are drawn from those values and the program's
// routing only.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
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
#include "settings.h"
#include "synth_link.h"
#include "visuals.h"

namespace mm2 {
namespace {

namespace ui = jclap::ui;
namespace vis = visuals;
using ui::Rect;

constexpr float kDesignWidth = 1640.0f;
constexpr float kDesignHeight = 1142.0f;
constexpr float kHeaderHeight = 64.0f;

// The grid: 16 point margins and gutters around three bands on each page.
constexpr float kMargin = 16.0f, kGutter = 16.0f;
constexpr float kInner = 1640.0f - 2 * kMargin; // a band's width
constexpr float kBand1Y = kHeaderHeight + kMargin, kBand1H = 340.0f;
constexpr float kBand2Y = kBand1Y + kBand1H + kGutter, kBand2H = 322.0f;
constexpr float kBand3Y = kBand2Y + kBand2H + kGutter, kBand3H = 352.0f;
static_assert(kBand3Y + kBand3H + kMargin == kDesignHeight, "the bands fill the panel");

// SOUND, band 1, left to right: sources, filter, voice, effects, out. Band 3: voice, effect modes, program.
constexpr float kAudioW[] = {440, 392, 200, 424, 88};
constexpr float kModesW[] = {660, 504, 412};
static_assert(440 + 392 + 200 + 424 + 88 + 4 * kGutter == kInner, "band 1 fills the width");
static_assert(660 + 504 + 412 + 2 * kGutter == kInner, "band 3 fills the width");
constexpr float audioX(int column) {
   float x = kMargin;
   for (int i = 0; i < column; ++i)
      x += kAudioW[i] + kGutter;
   return x;
}

// MODULATION: the mod sources, the matrix and the operators, top to bottom.
constexpr float kSourcesY = kBand1Y, kSourcesH = 300.0f;
constexpr float kMatrixY = kSourcesY + kSourcesH + kGutter, kMatrixH = 484.0f;
constexpr float kOpsY = kMatrixY + kMatrixH + kGutter, kOpsH = 230.0f;
static_assert(kOpsY + kOpsH + kMargin == kDesignHeight, "the modulation bands fill the panel");

enum Page { kSoundPage, kModPage, kArpPage };

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
      page_ = kSoundPage;
      buildSources();
      buildFilter();
      buildVoice();
      buildEffects();
      buildOut();
      buildEnvelopes();
      buildPlay();
      buildEffectModes();
      buildProgram();
      page_ = kModPage;
      buildModSources();
      buildMatrix();
      buildOperators();
      page_ = kArpPage;
      buildArp();
      buildPanelKnobs();
      page_ = -1;
      syncChannel();
      syncLink();
      syncRoutes();
      syncReadouts();
      startTimer(30);
   }

   void timerCallback() override {
      syncChannel();
      syncLink();
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
      if (tabs_->selected() != kSoundPage)
         return;
      canvas.setColor(0xff3d444e);
      const auto chevron = [&](float x, float y) {
         canvas.segment((x - 3) * s, (y - 6) * s, (x + 3) * s, y * s, 2.2f * s, true);
         canvas.segment((x + 3) * s, y * s, (x - 3) * s, (y + 6) * s, 2.2f * s, true);
      };
      for (int c = 0; c < 4; ++c)
         chevron(audioX(c) + kAudioW[c] + kGutter * 0.5f, kBand1Y + kBand1H * 0.5f);
   }

   // Shows `page` (kSoundPage...), as clicking its tab does.
   void selectPage(int page) { tabs_->select(page); }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small waveform glyph.
      const float pts[][2] = {{18, 33}, {26, 33}, {30, 23}, {35, 44}, {40, 16}, {45, 48}, {49, 27}, {53, 33}, {58, 33}};
      canvas.setColor(kOrange);
      for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); ++i)
         canvas.segment(pts[i][0] * s, pts[i][1] * s, pts[i + 1][0] * s, pts[i + 1][1] * s, 2.5f * s, true);
   }

   // ---- building blocks ----

   // Children go on the page being built (page_), or on every page while it is -1.
   template <typename T, typename... Args>
   T &add(Rect bounds, Args &&...args) {
      T &frame = DesignFrame::add<T>(bounds, std::forward<Args>(args)...);
      if (page_ >= 0)
         tabs_->addToPage(page_, frame);
      return frame;
   }
   void place(visage::Frame &frame, Rect bounds) {
      DesignFrame::place(frame, bounds);
      if (page_ >= 0)
         tabs_->addToPage(page_, frame);
   }

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
   // A dropdown for one of the settings in settings.h, named in capitals.
   ui::Dropdown &setting(int nrpn, unsigned accent, Rect r, const char *tip = nullptr) {
      std::vector<std::string> names = choiceNames(kSettings[indexOfSetting(nrpn)].choices);
      for (std::string &n : names)
         n = upper(n);
      auto &d = add<ui::Dropdown>(r, std::move(names));
      d.setAccent(accent);
      bindings_.bind(d, settingId(nrpn));
      if (tip)
         tips_.add(d, tip);
      return d;
   }
   // An on/off setting as a toggle button.
   ui::Button &toggle(int nrpn, const char *text, unsigned accent, Rect r, const char *tip = nullptr) {
      auto &b = add<ui::Button>(r, text);
      b.setAccent(accent);
      bindings_.bind(b, settingId(nrpn));
      if (tip)
         tips_.add(b, tip);
      return b;
   }
   // A number setting as a box with - and +, showing the synth's number for it.
   ui::NumberBox &number(int nrpn, unsigned accent, Rect r, const char *tip = nullptr) {
      const Setting &st = kSettings[indexOfSetting(nrpn)];
      auto &box = add<ui::NumberBox>(r, ui::ValueRange{static_cast<double>(st.min), static_cast<double>(st.max),
                                                       static_cast<double>(st.defaultValue), 1.0, false});
      box.setStepButtons(true);
      box.setAccent(accent);
      box.valueText = [&st](double v) { return settingText(st, static_cast<int>(std::lround(v))); };
      bindings_.bind(box, settingId(nrpn));
      if (tip)
         tips_.add(box, tip);
      return box;
   }
   // Whichever of the three fits the setting.
   visage::Frame &control(int nrpn, unsigned accent, Rect r, const char *tip = nullptr) {
      const Setting &st = kSettings[indexOfSetting(nrpn)];
      if (st.choices == Choices::Number)
         return number(nrpn, accent, r, tip);
      if (st.choices == Choices::OnOff)
         return toggle(nrpn, "ON", accent, r, tip);
      return setting(nrpn, accent, r, tip);
   }
   // A tile with a setting's name, its control and a line or two about it.
   void settingTile(int nrpn, const char *title, unsigned accent, Rect r, const char *about,
                    const char *about2 = nullptr, const char *tip = nullptr) {
      add<vis::Tile>(r);
      label(title, {r.x + 12, r.y + 10, r.w - 24, 14}, kDim, visage::Font::kLeft, 0.8f);
      control(nrpn, accent, {r.x + 12, r.y + 30, r.w - 24, 24}, tip);
      hint(about, {r.x + 12, r.y + 64, r.w - 24, 12}, visage::Font::kLeft);
      if (about2)
         hint(about2, {r.x + 12, r.y + 79, r.w - 24, 12}, visage::Font::kLeft);
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
      // The synth's programs: the bank compiled in, until READ BANK brings the ones it has now.
      tabs_ = &add<ui::TabBar>({300, 18, 312, 28}, std::vector<std::string>{"SOUND", "MODULATION", "ARP & KNOBS"});
      tabs_->setAccent(kOrange);
      tabs_->onChange = [this](int) { redraw(); };
      auto &bar = add<ui::PresetBar>({624, 18, 432, 28}, presets_, kPresetFormat, host_);
      bar_ = &bar;
      bar.setAccent(kOrange);
      bar.defaults = presetDefaults();
      bar.currentName = [this] { return host_.presetName(); };
      bar.setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      bar.onLoad = [this](const ui::Preset &p) {
         if (const HardwarePatch *patch = findPatch(patches_, p.name))
            host_.loadProgram(*patch);
         else
            ui::applyPreset(p, host_, presetDefaults());
      };
      add<ui::UpdateNotice>({1344, 47, 280, 16}).alignRight = true;

      // Reading the synth, through the Bitwig script. The line under the header says what's going on,
      // and what to do on the synth for a bank.
      place(readSound_, {1068, 18, 100, 28});
      place(part_, {1172, 18, 64, 28});
      place(readBank_, {1240, 18, 96, 28});
      readSound_.setAccent(kOrange);
      readBank_.setAccent(kOrange);
      readSound_.onClick = [this] {
         used_ = true;
         soundTicket_ = SynthLink::get().requestSound(partB_ ? 'B' : 'A');
      };
      part_.onClick = [this] {
         partB_ = !partB_;
         part_.setText(partB_ ? "PART B" : "PART A");
      };
      readBank_.onClick = [this] {
         used_ = true;
         waitingBank_ = !waitingBank_;
      };
      tips_.add(readSound_, "Reads the sound on the part shown beside it (A or B) into the panel: every knob and the mod "
                            "matrix routing, edits included. Nothing is sent to the synth. Needs the Bitwig script "
                            "and firmware 2.7 or later.");
      tips_.add(part_, "Which of the synth's two parts READ SOUND reads.");
      tips_.add(readBank_, "Lists the programs stored on the synth in the preset bar, in place of the built-in bank. "
                           "The synth only sends them from its own panel: press this, then UTILITY > U02 on the synth.");
      linkStatus_ = &label("", {624, 48, 712, 14}, kDim, visage::Font::kCenter, 0.8f);

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
      for (int o = 0; o < 3; ++o) {
         const float ry = y + 44 + o * 80;
         label("OSC " + std::to_string(o + 1), {x + 14, ry + 16, 50, 20}, kOscColors[o]);
         setting(27 + 4 * o, kOscColors[o], {x + 262, ry - 12, 164, 18},
                 o == 0 ? "Oscillator 1's algorithm (NRPN 27). The shape knob morphs within it."
                        : "The oscillator's algorithm, wavetables included (NRPN 31 / 35). The shape knob morphs "
                          "within it.");
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
      setting(125, kOrange, {x + 110, y + 6, 150, 20}, "Filter type (NRPN 125). The picture below is a lowpass guide.");
      setting(126, kOrange, {x + 266, y + 6, 114, 20}, "Filter character: how hard the filter is driven (NRPN 126).");
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
         hint("ALSO A MOD SOURCE", {bx + 64, y + 38, 160, 11}, visage::Font::kLeft);
         toggle(120 + e, "RESET TO ZERO", kOscColors[e], {bx + 340, y + 32, 120, 22},
                "Each note starts the envelope from zero instead of where it is (NRPN 120 / 121 / 122).");
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

   // ---- band 3: how the voice plays, the effect modes, the program ----

   // Tiles in a grid of `cols` columns inside a section's body.
   static Rect cell(Rect body, int cols, int rows, int i) {
      return ui::grid(body, cols, rows, 12.0f)[static_cast<size_t>(i)];
   }

   void buildPlay() {
      const float x = kMargin, y = kBand3Y, w = kModesW[0];
      section("VOICE & PLAY", {x, y, w, kBand3H});
      hint("SETTINGS NO CC REACHES, SENT AS NRPN", {x + 200, y + 10, w - 214, 12});
      const Rect body{x + 12, y + 34, w - 24, kBand3H - 46};
      settingTile(117, "VOICE MODE", kOrange, cell(body, 3, 2, 0), "MODERN AND VINTAGE ARE POLY,", "MONO AND LEGATO ONE VOICE",
                  "How the voices play (NRPN 117): Modern or Vintage polyphony, or one voice, Mono retriggering "
                  "and Legato not.");
      settingTile(118, "UNISON", kOrange, cell(body, 3, 2, 1), "VOICES STACKED ON EACH NOTE", nullptr,
                  "Unison voices per note (NRPN 118). The detune knob spreads them.");
      settingTile(119, "PAN SPREAD", kOrange, cell(body, 3, 2, 2), "HOW THE VOICES ARE PANNED", nullptr,
                  "How the voices are spread across the stereo field (NRPN 119).");
      settingTile(23, "BEND DOWN", kOrange, cell(body, 3, 2, 3), "SEMITONES, 0 TO -12", nullptr,
                  "The pitch bend's range down, in semitones (NRPN 23).");
      settingTile(24, "BEND UP", kOrange, cell(body, 3, 2, 4), "SEMITONES, 0 TO +12", nullptr,
                  "The pitch bend's range up, in semitones (NRPN 24).");
      settingTile(124, "OSC PHASE RESET", kOrange, cell(body, 3, 2, 5), "EACH NOTE STARTS THE OSCILLATORS",
                  "AT THE SAME PHASE", "Restarts the oscillators' phase on each note (NRPN 124).");
   }

   void buildEffectModes() {
      const float x = kMargin + kModesW[0] + kGutter, y = kBand3Y, w = kModesW[1];
      section("EFFECT MODES", {x, y, w, kBand3H});
      const Rect body{x + 12, y + 34, w - 24, kBand3H - 46};
      settingTile(127, "CHORUS", kBlue, cell(body, 2, 2, 0), "THE CHORUS AFTER THE FILTER", nullptr,
                  "Chorus type, or off (NRPN 127).");
      settingTile(128, "DELAY MODE", kBlue, cell(body, 2, 2, 1), "CLEAN, DARKER, OR WOBBLING", "REPEATS",
                  "How the delay's repeats sound (NRPN 128).");
      settingTile(108, "DELAY SYNC", kBlue, cell(body, 2, 2, 2), "A NOTE LENGTH AT THE PROGRAM'S",
                  "TEMPO, IN PLACE OF THE TIME KNOB",
                  "Locks the delay time to the program's tempo (NRPN 108). FREE uses the TIME knob (CC 26).");
      // A reminder of where the effect levels are, in the fourth cell.
      const Rect r = cell(body, 2, 2, 3);
      hint("THE DELAY, REVERB AND EQ KNOBS ARE", {r.x + 12, r.y + 34, r.w - 24, 12}, visage::Font::kLeft);
      hint("IN EFFECTS, ABOVE", {r.x + 12, r.y + 49, r.w - 24, 12}, visage::Font::kLeft);
   }

   void buildProgram() {
      const float x = kMargin + kModesW[0] + kModesW[1] + 2 * kGutter, y = kBand3Y, w = kModesW[2];
      section("PROGRAM", {x, y, w, kBand3H});
      const Rect body{x + 12, y + 34, w - 24, kBand3H - 46};
      settingTile(18, "TEMPO", kWhite, cell(body, 1, 3, 0), "BPM, FOR THE ARP AND TEMPO SYNC", nullptr,
                  "The program's tempo, 40 to 295 BPM (NRPN 18). The arpeggiator and the synced LFOs and delay "
                  "follow it.");
      settingTile(17, "PROGRAM VOLUME", kWhite, cell(body, 1, 3, 1), "SAVED IN THE PROGRAM, UNLIKE CC 7", nullptr,
                  "The program's own level, -24 to +24 (NRPN 17).");
      settingTile(9, "CATEGORY", kWhite, cell(body, 1, 3, 2), "WHERE THE SYNTH FILES IT", nullptr,
                  "The program's category on the synth (NRPN 9).");
   }

   // ---- MODULATION ----

   void buildModSources() {
      const float x = kMargin, y = kSourcesY, w = kInner;
      section("MOD SOURCES", {x, y, w, kSourcesH});
      hint("LIGHTS BLINK AT ROUGHLY EACH SPEED  \xC2\xB7  ENV 1-3 ON THE SOUND PAGE FEED THE MATRIX TOO",
           {x + 200, y + 10, w - 214, 12});
      for (int i = 0; i < 3; ++i) {
         const float tx = x + 12 + i * 376, ty = y + 34, tw = 364, th = 254;
         const unsigned c = kLfoColors[i];
         const std::string n = std::to_string(i + 1);
         add<vis::Tile>({tx, ty, tw, th});
         label("LFO " + n, {tx + 12, ty + 10, 60, 16}, c);
         hint("RATE", {tx + tw - 76, ty + 12, 44, 12});
         auto &led = add<ui::ActivityLed>({tx + tw - 28, ty + 10, 16, 16});
         led.setColor(c);
         led.fadeMs = 0;
         led.setLevel(1.0f);
         leds_[i] = &led;
         knob(76 + i, c, {tx + 12, ty + 40, 130, 140}, "SPEED");
         const float cx = tx + 160, cw = tw - 172;
         label("WAVE", {cx, ty + 40, cw, 12}, kDim, visage::Font::kLeft, 0.8f);
         setting(61 + 3 * i, c, {cx, ty + 56, cw, 22}, "The LFO's wave (NRPN 61 / 64 / 67).");
         label("TEMPO SYNC", {cx, ty + 90, cw, 12}, kDim, visage::Font::kLeft, 0.8f);
         setting(62 + 3 * i, c, {cx, ty + 106, cw, 22},
                 "Locks the LFO's speed to a note length at the program's tempo (NRPN 62 / 65 / 68). FREE uses "
                 "the SPEED knob.");
         label("TRIGGER", {cx, ty + 140, cw, 12}, kDim, visage::Font::kLeft, 0.8f);
         setting(63 + 3 * i, c, {cx, ty + 156, cw, 22},
                 "When the LFO restarts: per voice, once, on each key, or free running (NRPN 63 / 66 / 69).");
         syncHints_[i] = &hint("", {tx + 12, ty + 192, tw - 24, 12}, visage::Font::kLeft);
         feeds_[i] = &label("", {tx + 12, ty + 222, tw - 24, 14}, c, visage::Font::kLeft, 0.8f);
      }

      const float px = x + 12 + 3 * 376, pw = (w - 12 - (px - x) - 12) * 0.5f, ty = y + 34, th = 254;
      add<vis::Tile>({px, ty, pw, th});
      label("MOD WHEEL", {px + 12, ty + 10, pw - 24, 16}, kWheel);
      auto &wheel = add<vis::WheelStrip>({px + 16, ty + 40, 36, 170}, rangeOf(1));
      wire(wheel, 1, kWheel);
      wheelValue_ = &label("", {px + 66, ty + 44, pw - 78, 20}, kWhite, visage::Font::kLeft, 0.8f);
      caption(1, {px + 66, ty + 68, pw - 78, 11});
      hint("PERFORMANCE,", {px + 66, ty + 160, pw - 78, 12}, visage::Font::kLeft);
      hint("NOT SAVED", {px + 66, ty + 175, pw - 78, 12}, visage::Font::kLeft);
      feeds_[3] = &label("", {px + 12, ty + 222, pw - 24, 14}, kWheel, visage::Font::kLeft, 0.8f);

      const float kx = px + pw + 12;
      add<vis::Tile>({kx, ty, pw, th});
      label("MOD KNOB", {kx + 12, ty + 10, pw - 24, 16}, kGreen);
      knob(70, kGreen, {kx + 12, ty + 40, pw - 24, 140}, "");
      hint("THE M KNOB, NOT SAVED", {kx + 12, ty + 192, pw - 24, 12}, visage::Font::kCenter);
      feeds_[4] = &label("", {kx + 12, ty + 222, pw - 24, 14}, kGreen, visage::Font::kLeft, 0.8f);
   }

   void buildMatrix() {
      const float x = kMargin, y = kMatrixY, w = kInner;
      section("MOD MATRIX", {x, y, w, kMatrixH});
      hint("PICK A SOURCE AND DESTINATION, OR LOAD A PROGRAM FOR ITS ROUTING", {x + 500, y + 10, w - 514, 12});
      // Columns: slot, source, arrow, destination, where it is on the panel, amount, value, CC.
      constexpr float kSlot = 12, kSource = 52, kArrow = 252, kDest = 286, kWhere = 516, kAmount = 744,
                      kAmountW = 700, kValue = 1480, kCc = 1540;
      const auto head = [&](const char *t, float cx, float cw, visage::Font::Justification j = visage::Font::kLeft) {
         label(t, {x + cx, y + 34, cw, 12}, kDim, j, 0.8f);
      };
      head("SLOT", kSlot, 36);
      head("SOURCE", kSource, 180);
      head("DESTINATION", kDest, 200);
      head("ON THE PANEL", kWhere, 200);
      head("\xE2\x88\x92 REVERSED", kAmount, 120);
      head("NONE", kAmount, kAmountW, visage::Font::kCenter);
      head("MORE +", kAmount, kAmountW, visage::Font::kRight);
      head("AMOUNT", kValue - 20, 64, visage::Font::kRight);
      head("CC", kCc, 48);
      for (int s = 0; s < kModSlots; ++s) {
         const float ry = y + 56 + s * 38.0f;
         Row &row = rows_[s];
         if (s > 0)
            divider({x + 12, ry - 6, w - 24, 1});
         label(std::to_string(s + 1), {x + kSlot + 6, ry + 7, 30, 14}, kDim);
         row.source = &setting(sourceNrpn(s), kDim, {x + kSource, ry + 2, 180, 24});
         row.arrow = &add<vis::Arrow>({x + kArrow, ry + 8, 22, 12});
         row.destination = &setting(destinationNrpn(s), kDim, {x + kDest, ry + 2, 200, 24});
         row.where = &label("", {x + kWhere, ry + 7, 220, 14}, kDim);
         row.amount = &slider(52 + s, kNoise, {x + kAmount, ry - 2, kAmountW, 28});
         row.amount->valueText = [](double) { return std::string(); };
         row.value = &label("", {x + kValue, ry + 7, 44, 14}, kWhite, visage::Font::kRight);
         caption(52 + s, {x + kCc, ry + 8, 48, 12});
         tips_.add(*row.amount, modSentence(s, false, {}));
      }
      hint("THE ROUTING IS SENT AS NRPN AND THE AMOUNT AS A CC. A FAINT RING ON A SOUND PAGE KNOB SHOWS ROUGHLY "
           "HOW FAR ITS SLOT CAN PUSH IT. HOVER A ROW FOR THE SENTENCE.",
           {x + 12, y + kMatrixH - 22, w - 24, 12}, visage::Font::kLeft);
   }

   // The lag and the two multipliers: sources made from other sources, which the matrix can route.
   void buildOperators() {
      const float x = kMargin, y = kOpsY, w = kInner;
      section("OPERATORS", {x, y, w, kOpsH});
      hint("LAG, MULT 1 AND MULT 2 ARE SOURCES IN THE MATRIX", {x + 200, y + 10, w - 214, 12});
      const float ty = y + 34, th = kOpsH - 46, tw = (w - 24 - 24) / 3;
      for (int t = 0; t < 3; ++t) {
         const float tx = x + 12 + t * (tw + 12);
         add<vis::Tile>({tx, ty, tw, th});
         if (t == 0) {
            label("LAG", {tx + 12, ty + 10, 100, 16}, kOrange);
            hint("SMOOTHS A SOURCE: THE KNOB IS HOW SLOWLY IT FOLLOWS", {tx + 12, ty + 30, tw - 24, 12},
                 visage::Font::kLeft);
            label("SOURCE", {tx + 12, ty + 58, 120, 12}, kDim, visage::Font::kLeft, 0.8f);
            setting(100, kOrange, {tx + 12, ty + 74, 200, 24}, "The source the lag operator smooths (NRPN 100).");
            auto &lag = slider(62, kOrange, {tx + 12, ty + 116, tw - 24, 28}, "TIME");
            lag.valueText = [](double) { return std::string(); };
            lagValue_ = &label("", {tx + tw - 64, ty + 74, 52, 14}, kWhite, visage::Font::kRight);
            caption(62, {tx + tw - 64, ty + 90, 52, 12}, visage::Font::kRight);
            feedsOps_[0] = &label("", {tx + 12, ty + 158, tw - 24, 14}, kOrange, visage::Font::kLeft, 0.8f);
         } else {
            const int a = t == 1 ? 102 : 104;
            label(t == 1 ? "MULT 1" : "MULT 2", {tx + 12, ty + 10, 100, 16}, kOrange);
            hint("ONE SOURCE SCALED BY ANOTHER", {tx + 12, ty + 30, tw - 24, 12}, visage::Font::kLeft);
            const float cw = (tw - 24 - 40) * 0.5f;
            label("SOURCE A", {tx + 12, ty + 58, cw, 12}, kDim, visage::Font::kLeft, 0.8f);
            label("SOURCE B", {tx + 52 + cw, ty + 58, cw, 12}, kDim, visage::Font::kLeft, 0.8f);
            setting(a, kOrange, {tx + 12, ty + 74, cw, 24},
                    t == 1 ? "Multiplier 1's first source (NRPN 102)." : "Multiplier 2's first source (NRPN 104).");
            label("\xC3\x97", {tx + 12 + cw, ty + 76, 40, 20}, kWhite, visage::Font::kCenter, 1.0f);
            setting(a + 1, kOrange, {tx + 52 + cw, ty + 74, cw, 24},
                    t == 1 ? "Multiplier 1's second source (NRPN 103)." : "Multiplier 2's second source (NRPN 105).");
            feedsOps_[t] = &label("", {tx + 12, ty + 158, tw - 24, 14}, kOrange, visage::Font::kLeft, 0.8f);
         }
      }
   }

   // ---- ARP & KNOBS ----

   static constexpr float kArpH = kBand1H + kGutter + kBand2H;

   void buildArp() {
      const float x = kMargin, y = kBand1Y, w = kInner;
      section("ARPEGGIATOR", {x, y, w, kArpH});
      hint("THE ARPEGGIATOR'S SETTINGS ARE SAVED IN THE PROGRAM", {x + 200, y + 10, w - 214, 12});
      // The top row: on, latch, style, speed, gate, octaves, length, tempo.
      struct Item {
         int nrpn;
         const char *name;
         float width;
         const char *tip;
      };
      const Item items[] = {
         {130, "ON / OFF", 110, "Turns the arpeggiator on (NRPN 130)."},
         {131, "HOLD NOTES", 110, "Keeps the notes playing after the keys are let go (NRPN 131)."},
         {114, "STYLE", 200, "The order the held notes are played in (NRPN 114)."},
         {116, "SPEED", 150, "Each step's length, at the program's tempo (NRPN 116)."},
         {115, "GATE", 150, "How long each step sounds, 0-127 (NRPN 115)."},
         {132, "OCTAVES", 150, "How many octaves the pattern climbs (NRPN 132)."},
         {133, "LENGTH", 150, "Steps in the pattern, 1-16 (NRPN 133). Steps past it are skipped."},
         {18, "TEMPO", 170, "The program's tempo in BPM (NRPN 18), also on the SOUND page."},
      };
      float ix = x + 12;
      for (const Item &it : items) {
         label(it.name, {ix, y + 36, it.width, 12}, kDim, visage::Font::kLeft, 0.8f);
         const Rect r{ix, y + 52, it.width, 28};
         if (it.nrpn == 130 || it.nrpn == 131)
            toggle(it.nrpn, it.nrpn == 130 ? "ARP ON" : "LATCH", kOrange, r, it.tip);
         else
            control(it.nrpn, it.nrpn == 18 ? kWhite : kOrange, r, it.tip);
         ix += it.width + 16;
      }

      // The pattern, then each step's dropdown under its column.
      const Rect pattern{x + 12, y + 100, w - 24, 452};
      picture<vis::ArpPattern>(pattern, kOrange, static_cast<int>(kSettingIdBase));
      const std::vector<Rect> cols = ui::row({pattern.x, y + 564, pattern.w, 52}, 16);
      for (int i = 0; i < 16; ++i) {
         const Rect c = cols[static_cast<size_t>(i)];
         label(std::to_string(i + 1), {c.x, c.y, c.w, 14}, kDim, visage::Font::kCenter, 0.8f);
         steps_[i] = &setting(134 + i, kOrange, {c.x + 3, c.y + 20, c.w - 6, 24},
                              "What this step plays (NRPN 134-149): AUTO follows the style, TIE holds the last note, "
                              "1ST-6TH a held note by order, < AND > the one before or after, CHORD all of them.");
      }
      hint("BARS: THE HELD NOTE EACH STEP PLAYS (AUTO FOLLOWS THE STYLE)  \xC2\xB7  LINKS: TIES  \xC2\xB7  STACKS: "
           "CHORDS  \xC2\xB7  DIM: PAST THE LENGTH",
           {x + 12, y + kArpH - 22, w - 24, 12}, visage::Font::kLeft);
   }

   // What the synth's own F, Q and M knobs and its four encoders do in this program.
   void buildPanelKnobs() {
      const float x = kMargin, y = kBand3Y, w = kInner;
      section("THE SYNTH'S KNOBS", {x, y, w, kBand3H});
      hint("WHAT EACH KNOB ON THE SYNTH CONTROLS IN THIS PROGRAM  \xC2\xB7  SYSTEM: THE SYNTH'S GLOBAL CHOICE",
           {x + 220, y + 10, w - 234, 12});
      const char *names[] = {"F KNOB", "Q KNOB", "M KNOB", "ENCODER 1", "ENCODER 2", "ENCODER 3", "ENCODER 4"};
      const std::vector<Rect> tiles = ui::row({x + 12, y + 34, w - 24, kBand3H - 46}, 7, 12.0f);
      for (int i = 0; i < 7; ++i) {
         const Rect t = tiles[static_cast<size_t>(i)];
         add<vis::Tile>(t);
         label(names[i], {t.x + 12, t.y + 10, t.w - 24, 16}, i < 3 ? kOrange : kWhite);
         add<vis::KnobGlyph>({t.x + t.w * 0.5f - 75, t.y + 44, 150, 150}, i >= 3);
         label("CONTROLS", {t.x + 12, t.y + 216, t.w - 24, 12}, kDim, visage::Font::kLeft, 0.8f);
         setting(10 + i, i < 3 ? kOrange : kWhite, {t.x + 12, t.y + 232, t.w - 24, 24},
                 "What this knob on the synth controls in the program (NRPN 10-16).");
         hint("NRPN " + std::to_string(10 + i), {t.x + 12, t.y + 268, t.w - 24, 12}, visage::Font::kCenter);
      }
   }

   // ---- keeping it current ----

   void syncChannel() { channel_.setText("MIDI CH " + std::to_string(host_.channel())); }

   // The Bitwig script: what it's doing, a sound this panel asked for, and a bank once one arrives.
   void syncLink() {
      SynthLink &link = SynthLink::get();
      const SynthLink::Status st = link.status();
      std::string text;
      if (st.receiving)
         text = "RECEIVING PROGRAMS FROM THE SYNTH... " + std::to_string(st.received);
      else if (waitingBank_ && st.connected)
         text = "ON THE SYNTH: UTILITY > U02 DUMP, CHOOSE EVERY PROGRAM (FROM P001), THEN HOLD YES. "
                "PRESS READ BANK AGAIN TO STOP WAITING.";
      else if (!used_)
         text = "";
      else if (!st.connected)
         text = "READ SOUND AND READ BANK NEED THE BITWIG SCRIPT (BITWIG/MICROMONSTA2.CONTROL.JS, SEE THE README)";
      else if (st.askingSound)
         text = std::string("ASKING THE SYNTH FOR PART ") + (partB_ ? "B" : "A") + "'S SOUND...";
      else if (!st.error.empty())
         text = "READ FAILED: " + upper(st.error);
      else
         text = done_;
      linkStatus_->setText(text);

      const SynthLink::Sound sound = link.sound();
      if (soundTicket_ != 0 && sound.ticket == soundTicket_) {
         soundTicket_ = 0;
         HardwarePatch patch = sound.patch;
         // The synth doesn't say which program a part's sound came from; the name usually does.
         for (const HardwarePatch &p : patches_)
            if (p.name == patch.name)
               patch.program = p.program;
         host_.loadSound(patch);
         done_ = "READ THE SOUND ON PART " + std::string(partB_ ? "B" : "A") + ": " + upper(patch.name);
      }

      if (link.bankVersion() == shownBankVersion_)
         return;
      shownBankVersion_ = link.bankVersion();
      std::vector<HardwarePatch> read = link.bank();
      if (read.empty())
         return;
      waitingBank_ = false;
      done_ = std::to_string(read.size()) + " PROGRAMS FROM THE SYNTH IN THE PRESET BAR";
      patches_ = std::move(read);
      presets_ = toPresets(patches_);
      bar_->rescan();
   }

   static std::string upper(std::string s) {
      for (char &c : s)
         c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return s;
   }

   // The matrix rows follow the loaded program's routing, in plain English.
   void syncRoutes() {
      const int known = host_.routingKnown() ? 1 : 0;
      bool changed = known != shownKnown_;
      ModRoute routes[kModSlots];
      for (int s = 0; s < kModSlots; ++s) {
         routes[s] = host_.route(s);
         changed = changed || !(routes[s] == shownRoutes_[s]);
      }
      if (!changed)
         return;
      shownKnown_ = known;
      for (int s = 0; s < kModSlots; ++s) {
         shownRoutes_[s] = routes[s];
         Row &row = rows_[s];
         const bool used = known && routes[s].used();
         const unsigned color = used ? sourceColor(routes[s]) : kDim;
         row.source->setAccent(color);
         row.destination->setAccent(color);
         row.arrow->setColor(color);
         row.amount->setAccent(used ? color : 0xff4b515a);
         if (!known)
            row.where->setText(s == 0 ? "LOAD A PROGRAM TO SEE ITS ROUTING" : "");
         else
            row.where->setText(used ? targetOf(routes[s]).where : "");
         tips_.setText(*row.amount, modSentence(s, known, routes[s]));
      }
      // Which slots each LFO and performance control feeds.
      const auto feeds = [&](auto matches) {
         std::string list;
         for (int s = 0; s < kModSlots; ++s)
            if (known && routes[s].used() && matches(std::string(source(routes[s]).code)))
               list += (list.empty() ? "" : " \xC2\xB7 ") + std::to_string(s + 1);
         return list.empty() ? std::string("NOT ROUTED") : "FEEDS SLOT " + list;
      };
      for (int i = 0; i < 3; ++i) {
         const std::string lfo = "lfo" + std::to_string(i + 1);
         feeds_[i]->setText(feeds([&](const std::string &c) { return c.rfind(lfo, 0) == 0; }));
      }
      feeds_[3]->setText(feeds([](const std::string &c) { return c == "modwheel" || c.find("whl") != std::string::npos; }));
      const char *ops[] = {"lag", "mult1", "mult2"};
      for (int i = 0; i < 3; ++i)
         feedsOps_[i]->setText(feeds([&](const std::string &c) { return c == ops[i]; }));
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
         rows_[s].value->setColor(shownKnown_ > 0 && shownRoutes_[s].used() ? kWhite : kDim);
      }
      lagValue_->setText(std::to_string(toCcValue(value(62))));
      for (auto [readout, cc] : readouts_)
         readout->setText(std::to_string(toCcValue(value(cc))));
      wheelValue_->setText(std::to_string(toCcValue(value(1))));
      for (int i = 0; i < 3; ++i)
         syncHints_[i]->setText(std::lround(value(settingId(62 + 3 * i))) > 0
                                   ? "SYNCED TO THE TEMPO: THE SPEED KNOB IS IGNORED"
                                   : "");
      const long length = std::lround(value(settingId(133))) + 1;
      for (int i = 0; i < 16; ++i)
         steps_[i]->setAccent(i < length ? kOrange : 0xff4b515a);

      double reach[128] = {};
      unsigned color[128] = {};
      if (shownKnown_ > 0)
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
      ui::Dropdown *source = nullptr, *destination = nullptr;
      vis::Arrow *arrow = nullptr;
      ui::Label *where = nullptr, *value = nullptr;
      ui::Slider *amount = nullptr;
   };

   EditorHost &host_;
   SynthLink::Use linkUse_;
   std::vector<HardwarePatch> patches_ = factoryBank();
   std::vector<ui::Preset> presets_ = factoryPresets();
   ui::PresetBar *bar_ = nullptr;
   ui::Label *linkStatus_ = nullptr;
   uint32_t shownBankVersion_ = 0;
   int soundTicket_ = 0;
   bool partB_ = false, waitingBank_ = false, used_ = false;
   std::string done_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::Knob *knobs_[128] = {};
   std::vector<vis::Picture *> pictures_;
   Row rows_[kModSlots];
   std::vector<std::pair<ui::Label *, int>> readouts_;
   ui::Label *lagValue_ = nullptr, *wheelValue_ = nullptr;
   ui::Label *feeds_[5] = {}; // LFO 1-3, mod wheel, mod knob
   ui::Label *feedsOps_[3] = {}; // lag, mult 1, mult 2
   ui::Label *syncHints_[3] = {};
   ui::Dropdown *steps_[16] = {};
   ui::TabBar *tabs_ = nullptr;
   int page_ = -1; // the page being built
   ui::ActivityLed *leds_[3] = {};
   double lfoPhase_[3] = {};
   int shownKnown_ = -1;
   ModRoute shownRoutes_[kModSlots];
   ui::Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
   ui::Button readSound_{"READ SOUND"}, part_{"PART A"}, readBank_{"READ BANK"};
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath, int page) {
   Panel panel(host);
   panel.selectPage(page);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace mm2
