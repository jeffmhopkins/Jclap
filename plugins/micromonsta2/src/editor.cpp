// Visage panel for the MicroMonsta 2 controller, laid out on a fixed kDesignWidth x kDesignHeight
// grid and scaled to the window, which keeps its aspect ratio. It is drawn like JSynth's: widgets
// on dark translucent cards.
//
// Three pages, picked by the tabs in the header:
//
//    SYNTH          oscillators, voice, filter, LFOs, envelopes and effects: the whole sound;
//    MOD MATRIX     the ten slots, the mod wheel and knob, and the lag and multiply operators;
//    ARP & PROGRAM  the arpeggiator, the program's tempo, volume and category, and what the
//                   synth's own knobs control.
//
// Every knob is one CC; the dropdowns, switches, toggles and number boxes are the settings no CC
// reaches, sent as NRPN (settings.h). The pictures in visuals.h are drawn from those values and the
// program's routing only.

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
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
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
constexpr float kHeaderHeight = 60.0f;

// The grid: 16 point margins and gutters between cards.
constexpr float kMargin = 16.0f, kGutter = 16.0f;
constexpr float kInner = kDesignWidth - 2 * kMargin;
constexpr float kTop = kHeaderHeight + kMargin;

// SYNTH: two columns of three cards. Left: oscillators, filter, envelopes. Right: voice, LFOs, effects.
constexpr float kLeftX = kMargin, kLeftW = 980.0f;
constexpr float kRightX = kLeftX + kLeftW + kGutter, kRightW = kInner - kLeftW - kGutter;
constexpr float kRow1Y = kTop, kRow1H = 336.0f;
constexpr float kRow2Y = kRow1Y + kRow1H + kGutter, kRow2H = 316.0f;
constexpr float kRow3Y = kRow2Y + kRow2H + kGutter, kRow3H = kDesignHeight - kMargin - kRow3Y;
static_assert(kRow3H > 340.0f, "the envelopes and effects have room");

// MOD MATRIX and ARP & PROGRAM: a large card on top, and a row of cards under it.
constexpr float kMatrixH = 680.0f;
constexpr float kBottomY = kTop + kMatrixH + kGutter, kBottomH = kDesignHeight - kMargin - kBottomY;
constexpr float kPerfW = 220.0f, kOpW = (kInner - 2 * kPerfW - 4 * kGutter) / 3;
constexpr float kProgramW = 440.0f;

enum Page { kSynthPage, kMatrixPage, kArpPage };

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;
constexpr unsigned kNoise = 0xffc9cdd3;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kDim = 0xff9aa3b5;
constexpr unsigned kHint = 0xff6b7385;
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


// The program's name in a box: click it to type a new one (8 characters), Enter sends it.
class NameField : public visage::Frame {
public:
   std::function<std::string()> current;
   std::function<void(const std::string &)> onCommit;

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = vis::glassTheme();
      const float h = height(), r = h * 0.18f;
      c.setColor(t.button);
      c.roundedRectangle(0, 0, width(), h, r);
      c.setColor(hover_ ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), h, r, 1.0f);
      if (editor_ && editor_->isVisible())
         return;
      c.setColor(kWhite);
      c.text(current ? current() : std::string(), ui::font(h * 0.5f), visage::Font::kLeft, h * 0.4f, 0, width(), h);
      c.setColor(kHint);
      c.text("CLICK TO RENAME", ui::font(h * 0.36f), visage::Font::kRight, 0, 0, width() - h * 0.4f, h);
   }
   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &) override { open(); }
   void resized() override {
      if (editor_)
         layoutEditor();
   }
   // Redraws when the name changed (the panel's timer calls this).
   void refresh() {
      const std::string now = current ? current() : std::string();
      if (now != shown_) {
         shown_ = now;
         redraw();
      }
   }

private:
   class Editor : public visage::TextEditor {
   public:
      std::function<void()> onClickAway;
      void focusChanged(bool is_focused, bool was_clicked) override {
         visage::TextEditor::focusChanged(is_focused, was_clicked);
         if (!is_focused && onClickAway)
            onClickAway();
      }
   };

   void layoutEditor() {
      const float h = height();
      editor_->setBounds(h * 0.3f, 2, width() - h * 0.6f, h - 4);
      editor_->setFont(ui::font(h * 0.5f));
   }
   void open() {
      if (!editor_) {
         editor_ = std::make_unique<Editor>();
         editor_->setMaxCharacters(kNameLength);
         editor_->onEnterKey() += [this] { close(true); };
         editor_->onEscapeKey() += [this] { close(false); };
         editor_->onClickAway = [this] { close(false); };
         addChild(editor_.get(), false);
      }
      layoutEditor();
      editor_->setText(current ? current() : std::string());
      editor_->setVisible(true);
      editor_->requestKeyboardFocus();
      editor_->selectAll();
      redraw();
   }
   void close(bool apply) {
      if (!editor_ || !editor_->isVisible())
         return;
      editor_->setVisible(false);
      if (apply && onCommit)
         onCommit(editor_->text().toUtf8());
      redraw();
   }

   std::unique_ptr<Editor> editor_;
   std::string shown_;
   bool hover_ = false;
};

// Whether a widget takes a theme, so add() can give every widget the glass look.
template <typename T, typename = void>
struct HasTheme : std::false_type {};
template <typename T>
struct HasTheme<T, std::void_t<decltype(std::declval<T &>().setTheme(std::declval<const ui::Theme &>()))>>
   : std::true_type {};

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      page_ = kSynthPage;
      buildOscillators();
      buildVoice();
      buildFilter();
      buildLfos();
      buildEnvelopes();
      buildEffects();
      page_ = kMatrixPage;
      buildMatrix();
      buildPerformance();
      buildOperators();
      page_ = kArpPage;
      buildArp();
      buildProgram();
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
      name_->refresh();
      blinkLfos();
   }

   void draw(visage::Canvas &canvas) override {
      const float s = scale();
      canvas.setColor(visage::Brush::vertical(visage::Color(0xff111826), visage::Color(0xff06080d)));
      canvas.fill(0, 0, width(), height());
      canvas.setColor(0xff070910);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      canvas.setColor(visage::Color(0x22ffffff));
      canvas.fill(0, (kHeaderHeight - 1) * s, width(), 1 * s);
      drawLogo(canvas, s);
      canvas.setColor(kOrange);
      canvas.text("Micro", ui::font(26 * s), visage::Font::kLeft, 62 * s, 8 * s, 100 * s, 44 * s);
      canvas.setColor(kWhite);
      canvas.text("Monsta 2", ui::font(26 * s), visage::Font::kLeft, 135 * s, 8 * s, 160 * s, 44 * s);
   }

   // Shows `page` (kSynthPage...), as clicking its tab does.
   void selectPage(int page) { tabs_->select(page); }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small waveform glyph.
      const float pts[][2] = {{18, 31}, {26, 31}, {30, 21}, {35, 42}, {40, 14}, {45, 46}, {49, 25}, {53, 31}, {58, 31}};
      canvas.setColor(kOrange);
      for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); ++i)
         canvas.segment(pts[i][0] * s, pts[i][1] * s, pts[i + 1][0] * s, pts[i + 1][1] * s, 2.5f * s, true);
   }

   // ---- building blocks ----

   // Children get the glass theme, and go on the page being built (page_), or every page while it is -1.
   template <typename T, typename... Args>
   T &add(Rect bounds, Args &&...args) {
      T &frame = DesignFrame::add<T>(bounds, std::forward<Args>(args)...);
      adopt(frame);
      return frame;
   }
   template <typename T>
   void place(T &frame, Rect bounds) {
      DesignFrame::place(frame, bounds);
      adopt(frame);
   }
   template <typename T>
   void adopt(T &frame) {
      if constexpr (HasTheme<T>::value)
         frame.setTheme(vis::glassTheme());
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

   // A knob for a CC, its name over it and its value under it; label nullptr uses the CC's short
   // label. Its CC is in its hover help.
   ui::Knob &knob(int cc, unsigned accent, Rect cell, const char *label = nullptr) {
      auto &k = add<ui::Knob>(cell, rangeOf(cc));
      k.setLabel(label ? label : param(cc).label);
      wire(k, cc, accent);
      tips_.add(k, std::string(param(cc).name) + " (CC " + std::to_string(cc) + ")");
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
   // A setting with a few choices, all shown; `names` replaces the choices' own names.
   ui::SegmentedSwitch &segments(int nrpn, unsigned accent, Rect r, std::vector<std::string> names = {},
                                 const char *tip = nullptr) {
      if (names.empty()) {
         names = choiceNames(kSettings[indexOfSetting(nrpn)].choices);
         for (std::string &n : names)
            n = upper(n);
      }
      auto &sw = add<ui::SegmentedSwitch>(r, std::move(names));
      sw.setAccent(accent);
      bindings_.bind(sw, settingId(nrpn));
      if (tip)
         tips_.add(sw, tip);
      return sw;
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
   ui::Label &label(const std::string &text, Rect r, unsigned color = kWhite,
                    visage::Font::Justification j = visage::Font::kLeft, float fontScale = 0.78f) {
      return add<ui::Label>(r, text, color, j, fontScale);
   }
   // A control's small grey name, over it.
   ui::Label &name(const std::string &text, Rect r, visage::Font::Justification j = visage::Font::kLeft) {
      return label(text, r, kDim, j, 0.85f);
   }
   // Small notes, such as a card's right-hand remark.
   ui::Label &hint(const std::string &text, Rect r, visage::Font::Justification j = visage::Font::kRight) {
      return label(text, r, kHint, j, 0.8f);
   }
   void card(const char *title, unsigned accent, Rect r) { add<vis::Card>(r, title, accent, r.h); }
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
   static int settingParam(int nrpn) { return static_cast<int>(settingId(nrpn)); }

   // ---- header ----

   void buildHeader() {
      tabs_ = &add<ui::TabBar>({300, 16, 330, 28}, std::vector<std::string>{"SYNTH", "MOD MATRIX", "ARP & PROGRAM"});
      tabs_->setAccent(kOrange);
      tabs_->onChange = [this](int) { redraw(); };

      // The synth's programs: the bank compiled in, until READ BANK brings the ones it has now.
      auto &bar = add<ui::PresetBar>({644, 16, 412, 28}, presets_, kPresetFormat, host_);
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
      add<ui::UpdateNotice>({1344, 45, 280, 14}).alignRight = true;

      // Reading the synth, through the Bitwig script. The line under the header says what's going on,
      // and what to do on the synth for a bank.
      place(readSound_, {1068, 16, 100, 28});
      place(part_, {1172, 16, 64, 28});
      place(readBank_, {1240, 16, 96, 28});
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
      tips_.add(readSound_, "Reads the sound on the part shown beside it (A or B) into the panel: every knob and "
                            "setting, edits included. Nothing is sent to the synth. Needs the Bitwig script and "
                            "firmware 2.7 or later.");
      tips_.add(part_, "Which of the synth's two parts READ SOUND reads.");
      tips_.add(readBank_, "Lists the programs stored on the synth in the preset bar, in place of the built-in bank. "
                           "The synth only sends them from its own panel: press this, then UTILITY > U02 on the synth.");
      linkStatus_ = &label("", {644, 46, 692, 13}, kDim, visage::Font::kCenter, 0.8f);

      place(chDown_, {1344, 16, 30, 28});
      place(channel_, {1378, 16, 116, 28});
      place(chUp_, {1498, 16, 30, 28});
      place(sendAll_, {1536, 16, 88, 28});
      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
   }

   // ---- SYNTH ----

   void buildOscillators() {
      const float x = kLeftX, y = kRow1Y, w = kLeftW;
      card("OSCILLATORS", kOrange, {x, y, w, kRow1H});
      hint("WAVES ARE GUIDES FROM THE ALGORITHM AND SHAPE", {x + 300, y + 11, 450, 12});
      const char *heads[] = {"SHAPE", "TUNE", "FINE", "LEVEL"};
      for (int o = 0; o < 3; ++o) {
         const float ry = y + 40 + o * 96.0f;
         const unsigned c = kOscColors[o];
         label("OSC " + std::to_string(o + 1), {x + 16, ry + 6, 80, 18}, c, visage::Font::kLeft, 0.9f);
         setting(27 + 4 * o, c, {x + 16, ry + 34, 156, 24},
                 o == 0 ? "Oscillator 1's algorithm (NRPN 27), FM included. The shape knob morphs within it."
                        : "The oscillator's algorithm, wavetables included (NRPN 31 / 35). The shape knob morphs "
                          "within it.");
         picture<vis::OscWave>({x + 184, ry, 244, 84}, c, settingParam(27 + 4 * o), 39 + 3 * o, o == 0);
         for (int k = 0; k < 4; ++k)
            knob(k < 3 ? 39 + 3 * o + k : 48 + o, c, {x + 440 + k * 80.0f, ry - 4, 80, 92}, heads[k]);
      }
      // Noise, the detune that spreads all three, the mix and the phase reset.
      const float nx = x + 772, nw = w - 772 - 16;
      divider({nx - 12, y + 40, 1, kRow1H - 56});
      knob(51, kNoise, {nx, y + 36, nw * 0.5f, 92}, "NOISE");
      knob(34, kOrange, {nx + nw * 0.5f, y + 36, nw * 0.5f, 92}, "OSC DETUNE");
      name("MIX INTO FILTER", {nx, y + 148, nw, 12});
      const unsigned mixColors[4] = {kOrange, kBlue, kPurple, kNoise};
      picture<vis::MixBar>({nx, y + 166, nw, 12}, mixColors);
      name("PHASE", {nx, y + 204, nw, 12});
      toggle(124, "RESET ON EACH NOTE", kOrange, {nx, y + 220, nw, 26},
             "Restarts the oscillators' phase on each note (NRPN 124).");
      hint("OFF: FREE RUNNING", {nx, y + 252, nw, 12}, visage::Font::kLeft);
   }

   void buildVoice() {
      const float x = kRightX, y = kRow1Y, w = kRightW;
      card("VOICE", kOrange, {x, y, w, kRow1H});
      name("MODE", {x + 16, y + 40, 300, 12});
      segments(117, kOrange, {x + 16, y + 56, 316, 26}, {},
               "How the voices play (NRPN 117): Modern or Vintage polyphony, or one voice, Mono retriggering and "
               "Legato not.");
      name("UNISON", {x + 348, y + 40, 200, 12});
      segments(118, kOrange, {x + 348, y + 56, w - 364, 26}, {"OFF", "2", "3", "6"},
               "Unison voices per note (NRPN 118). DETUNE spreads them.");
      knobRow({5, 33, 91, 35, 36, 37, 7}, kOrange, {x + 8, y + 96, w - 16, 96});
      knobs_[36]->setLabel("VEL > VCA");
      knobs_[37]->setLabel("VEL > FILT");
      // The performance controls, beside the voice they play.
      add<vis::Tile>({x + 16, y + 204, 184, 116});
      auto &wheel = add<vis::WheelStrip>({x + 26, y + 212, 30, 100}, rangeOf(1));
      wire(wheel, 1, kWheel);
      tips_.add(wheel, "Mod wheel (CC 1). Not saved in the program.");
      label("WHEEL", {x + 60, y + 214, 50, 12}, kWheel, visage::Font::kLeft, 0.85f);
      frontWheel_ = &label("", {x + 60, y + 230, 50, 16}, kWhite, visage::Font::kLeft, 0.8f);
      knob(70, kGreen, {x + 104, y + 206, 92, 100}, "MOD KNOB");
      name("PAN SPREAD", {x + 216, y + 208, 200, 12});
      segments(119, kOrange, {x + 216, y + 224, 200, 26}, {},
               "How PAN SPREAD places the voices in the stereo field (NRPN 119).");
      name("BEND DOWN / UP", {x + 432, y + 208, 164, 12});
      number(23, kOrange, {x + 432, y + 224, 80, 26}, "The pitch bend's range down, in semitones (NRPN 23).");
      number(24, kOrange, {x + 516, y + 224, 80, 26}, "The pitch bend's range up, in semitones (NRPN 24).");
      hint("VOLUME (CC 7) IS NOT SAVED IN THE PROGRAM", {x + 216, y + 300, 380, 12}, visage::Font::kLeft);
   }

   void buildFilter() {
      const float x = kLeftX, y = kRow2Y, w = kLeftW;
      card("FILTER", kOrange, {x, y, w, kRow2H});
      setting(125, kOrange, {x + 100, y + 8, 150, 24}, "Filter type (NRPN 125). The picture is a lowpass guide.");
      segments(126, kOrange, {x + 262, y + 8, 260, 24}, {},
               "Filter character: how hard the filter is driven (NRPN 126).");
      hint("LINE: CUTOFF AND RESONANCE  \xC2\xB7  DASHED: ENV AMOUNT AT ITS PEAK", {x + 540, y + 14, w - 556, 12});
      picture<vis::FilterCurve>({x + 16, y + 44, w - 32, 156});
      knobRow({74, 71, 88, 89, 90}, kOrange, {x + 16, y + 210, w - 32, 96});
   }

   void buildLfos() {
      const float x = kRightX, y = kRow2Y, w = kRightW;
      card("LFOS", kCyan, {x, y, w, kRow2H});
      hint("LIGHTS BLINK AT ROUGHLY EACH SPEED", {x + 200, y + 11, w - 216, 12});
      for (int i = 0; i < 3; ++i) {
         const float ry = y + 40 + i * 90.0f;
         const unsigned c = kLfoColors[i];
         if (i > 0)
            divider({x + 16, ry - 6, w - 32, 1});
         label("LFO " + std::to_string(i + 1), {x + 16, ry + 4, 56, 18}, c, visage::Font::kLeft, 0.9f);
         auto &led = add<ui::ActivityLed>({x + 74, ry + 6, 14, 14});
         led.setColor(c);
         led.fadeMs = 0;
         led.setLevel(1.0f);
         leds_[i] = &led;
         feeds_[i] = &label("", {x + 16, ry + 32, 80, 12}, c, visage::Font::kLeft, 0.8f);
         syncHints_[i] = &hint("", {x + 16, ry + 50, 80, 24}, visage::Font::kLeft);
         picture<vis::LfoShape>({x + 100, ry + 2, 140, 74}, c, settingParam(61 + 3 * i));
         knob(76 + i, c, {x + 246, ry - 4, 80, 88}, "SPEED");
         name("WAVE", {x + 336, ry, 124, 11});
         setting(61 + 3 * i, c, {x + 336, ry + 12, 124, 22}, "The LFO's wave (NRPN 61 / 64 / 67).");
         name("TEMPO SYNC", {x + 336, ry + 40, 124, 11});
         setting(62 + 3 * i, c, {x + 336, ry + 52, 124, 22},
                 "Locks the LFO's speed to a note length at the program's tempo (NRPN 62 / 65 / 68). FREE uses "
                 "the SPEED knob.");
         name("TRIGGER", {x + 472, ry, 124, 11});
         setting(63 + 3 * i, c, {x + 472, ry + 12, 124, 22},
                 "When the LFO restarts: per voice, once, on each key, or free running (NRPN 63 / 66 / 69).");
      }
   }

   // Times sit on a square-root curve after a minimum width, so a zero attack or decay still leaves
   // room between the corners to grab each one.
   static constexpr float kMinStage = 0.14f;

   void buildEnvelopes() {
      const float x = kLeftX, y = kRow3Y, w = kLeftW;
      card("ENVELOPES", kOrange, {x, y, w, kRow3H});
      hint("DRAG A CORNER, OR DRAG OR TYPE IN A BOX  \xC2\xB7  ENV 1-3 ARE MOD SOURCES TOO", {x + 300, y + 11, w - 316, 12});
      const int ccs[3][4] = {{73, 75, 79, 72}, {80, 81, 82, 83}, {84, 85, 86, 87}};
      const char *names[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
      const float ew = (w - 32 - 2 * 16) / 3;
      for (int e = 0; e < 3; ++e) {
         const float ex = x + 16 + e * (ew + 16);
         const unsigned c = kOscColors[e];
         label("ENV " + std::to_string(e + 1), {ex, y + 42, 80, 18}, c, visage::Font::kLeft, 0.9f);
         toggle(120 + e, "RESET TO 0", c, {ex + ew - 96, y + 40, 96, 22},
                "Each note starts the envelope from zero instead of where it is (NRPN 120 / 121 / 122).");
         auto &env = add<ui::EnvelopeEditor>({ex, y + 70, ew, 220}, rangeOf(ccs[e][0]), rangeOf(ccs[e][1]),
                                             rangeOf(ccs[e][2]), rangeOf(ccs[e][3]));
         env.setAccent(c);
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
         const std::vector<Rect> cells = ui::row({ex, y + 300, ew, 50}, 4, 6.0f);
         for (int s = 0; s < 4; ++s) {
            const Rect cl = cells[static_cast<size_t>(s)];
            name(names[s], {cl.x, cl.y, cl.w, 12}, visage::Font::kCenter);
            auto &box = add<ui::NumberBox>({cl.x, cl.y + 16, cl.w, 26}, rangeOf(ccs[e][s]));
            wire(box, ccs[e][s], c);
            tips_.add(box, std::string(param(ccs[e][s]).name) + " (CC " + std::to_string(ccs[e][s]) + ")");
         }
      }
   }

   void buildEffects() {
      const float x = kRightX, y = kRow3Y, w = kRightW;
      card("EFFECTS", kBlue, {x, y, w, kRow3H});
      hint("PICTURES ARE GUIDES FROM THE KNOBS", {x + 200, y + 11, w - 216, 12});
      // Delay: its mode and tempo sync, the echoes, then time, feedback and level.
      label("DELAY", {x + 16, y + 40, 100, 18}, kBlue, visage::Font::kLeft, 0.9f);
      name("MODE", {x + 16, y + 66, 60, 12});
      setting(128, kBlue, {x + 60, y + 62, 112, 22}, "How the delay's repeats sound (NRPN 128).");
      name("SYNC", {x + 16, y + 98, 60, 12});
      setting(108, kBlue, {x + 60, y + 94, 112, 22},
              "Locks the delay time to a note length at the program's tempo (NRPN 108). FREE uses the TIME knob.");
      picture<vis::DelayTaps>({x + 184, y + 40, 148, 76}, kBlue);
      for (int k = 0; k < 3; ++k)
         knob(26 + k, kBlue, {x + 340 + k * 86.0f, y + 32, 86, 92});
      divider({x + 16, y + 130, w - 32, 1});
      // Reverb.
      label("REVERB", {x + 16, y + 144, 100, 18}, kPurple, visage::Font::kLeft, 0.9f);
      picture<vis::ReverbTail>({x + 184, y + 144, 148, 76}, kPurple);
      for (int k = 0; k < 3; ++k)
         knob(29 + k, kPurple, {x + 340 + k * 86.0f, y + 136, 86, 92});
      divider({x + 16, y + 234, w - 32, 1});
      // Chorus and the tilt EQ.
      label("CHORUS", {x + 16, y + 248, 100, 18}, kGreen, visage::Font::kLeft, 0.9f);
      segments(127, kGreen, {x + 16, y + 276, 156, 26}, {"OFF", "1", "2", "1+2"},
               "The chorus: off, type 1, type 2 or both (NRPN 127).");
      picture<vis::EqTilt>({x + 184, y + 248, 148, 76}, kGreen);
      knob(25, kGreen, {x + 340, y + 240, 86, 92}, "TILT EQ");
      hint("64 IS FLAT", {x + 432, y + 280, 160, 12}, visage::Font::kLeft);
   }

   // ---- MOD MATRIX ----

   void buildMatrix() {
      const float x = kMargin, y = kTop, w = kInner;
      card("MOD MATRIX", kOrange, {x, y, w, kMatrixH});
      hint("PICK A SOURCE AND DESTINATION, OR LOAD A PROGRAM FOR ITS ROUTING  \xC2\xB7  THE ROUTING IS SENT AS NRPN, "
           "THE AMOUNT AS A CC",
           {x + 300, y + 11, w - 316, 12});
      // Columns: slot, source, arrow, destination, where it is on the panel, amount, value, CC.
      constexpr float kSlot = 16, kSource = 56, kArrow = 256, kDest = 290, kWhere = 520, kAmount = 748,
                      kAmountW = 700, kValue = 1480, kCc = 1540;
      const auto head = [&](const char *t, float cx, float cw, visage::Font::Justification j = visage::Font::kLeft) {
         name(t, {x + cx, y + 40, cw, 12}, j);
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
         const float ry = y + 64 + s * 58.0f;
         Row &row = rows_[s];
         if (s > 0)
            divider({x + 16, ry - 12, w - 32, 1});
         label(std::to_string(s + 1), {x + kSlot + 6, ry + 7, 30, 16}, kDim);
         row.source = &setting(sourceNrpn(s), kDim, {x + kSource, ry + 2, 180, 26});
         row.arrow = &add<vis::Arrow>({x + kArrow, ry + 9, 22, 12});
         row.destination = &setting(destinationNrpn(s), kDim, {x + kDest, ry + 2, 200, 26});
         row.where = &label("", {x + kWhere, ry + 8, 220, 14}, kDim);
         row.amount = &slider(52 + s, kNoise, {x + kAmount, ry - 2, kAmountW, 30});
         row.amount->valueText = [](double) { return std::string(); };
         row.value = &label("", {x + kValue, ry + 8, 44, 14}, kWhite, visage::Font::kRight);
         hint("CC " + std::to_string(52 + s), {x + kCc, ry + 9, 48, 12}, visage::Font::kLeft);
         tips_.add(*row.amount, modSentence(s, false, {}));
      }
      hint("A FAINT RING ON A SYNTH PAGE KNOB SHOWS ROUGHLY HOW FAR ITS SLOT CAN PUSH IT. HOVER A ROW FOR THE SENTENCE.",
           {x + 16, y + kMatrixH - 26, w - 32, 12}, visage::Font::kLeft);
   }

   // The mod wheel and mod knob: performance controls, not saved in the program.
   void buildPerformance() {
      const float y = kBottomY, h = kBottomH;
      float x = kMargin;
      card("MOD WHEEL", kWheel, {x, y, kPerfW, h});
      auto &wheel = add<vis::WheelStrip>({x + 20, y + 44, 40, h - 64}, rangeOf(1));
      wire(wheel, 1, kWheel);
      tips_.add(wheel, "Mod wheel (CC 1). Not saved in the program.");
      wheelValue_ = &label("", {x + 76, y + 48, kPerfW - 92, 22}, kWhite, visage::Font::kLeft, 0.8f);
      hint("CC 1", {x + 76, y + 72, kPerfW - 92, 12}, visage::Font::kLeft);
      hint("NOT SAVED", {x + 76, y + 88, kPerfW - 92, 12}, visage::Font::kLeft);
      feeds_[3] = &label("", {x + 76, y + h - 34, kPerfW - 92, 14}, kWheel, visage::Font::kLeft, 0.8f);

      x += kPerfW + kGutter;
      card("MOD KNOB", kGreen, {x, y, kPerfW, h});
      // The SYNTH page's mod knob keeps the reach ring.
      ui::Knob *front = knobs_[70];
      knob(70, kGreen, {x + 16, y + 44, kPerfW - 32, 150}, "M");
      knobs_[70] = front;
      hint("THE SYNTH'S M KNOB, NOT SAVED", {x + 16, y + 206, kPerfW - 32, 12}, visage::Font::kCenter);
      feeds_[4] = &label("", {x + 16, y + h - 34, kPerfW - 32, 14}, kGreen, visage::Font::kCenter, 0.8f);
   }

   // The lag and the two multipliers: sources made from other sources, which the matrix can route.
   void buildOperators() {
      const float y = kBottomY, h = kBottomH;
      float x = kMargin + 2 * (kPerfW + kGutter);
      card("LAG", kOrange, {x, y, kOpW, h});
      hint("SMOOTHS A SOURCE", {x + 100, y + 11, kOpW - 116, 12});
      name("SOURCE", {x + 16, y + 44, 200, 12});
      setting(100, kOrange, {x + 16, y + 60, kOpW - 32, 26}, "The source the lag operator smooths (NRPN 100).");
      knob(62, kOrange, {x + 16, y + 100, kOpW - 32, 130}, "TIME");
      feedsOps_[0] = &label("", {x + 16, y + h - 34, kOpW - 32, 14}, kOrange, visage::Font::kCenter, 0.8f);
      for (int m = 0; m < 2; ++m) {
         x += kOpW + kGutter;
         const int a = m == 0 ? 102 : 104;
         card(m == 0 ? "MULT 1" : "MULT 2", kOrange, {x, y, kOpW, h});
         hint("ONE SOURCE SCALED BY ANOTHER", {x + 100, y + 11, kOpW - 116, 12});
         name("SOURCE A", {x + 16, y + 44, 200, 12});
         setting(a, kOrange, {x + 16, y + 60, kOpW - 32, 26},
                 m == 0 ? "Multiplier 1's first source (NRPN 102)." : "Multiplier 2's first source (NRPN 104).");
         label("\xC3\x97", {x + 16, y + 96, kOpW - 32, 26}, kWhite, visage::Font::kCenter, 0.9f);
         name("SOURCE B", {x + 16, y + 128, 200, 12});
         setting(a + 1, kOrange, {x + 16, y + 144, kOpW - 32, 26},
                 m == 0 ? "Multiplier 1's second source (NRPN 103)." : "Multiplier 2's second source (NRPN 105).");
         feedsOps_[m + 1] = &label("", {x + 16, y + h - 34, kOpW - 32, 14}, kOrange, visage::Font::kCenter, 0.8f);
      }
   }

   // ---- ARP & PROGRAM ----

   void buildArp() {
      const float x = kMargin, y = kTop, w = kInner, h = kMatrixH;
      card("ARPEGGIATOR", kOrange, {x, y, w, h});
      hint("SAVED IN THE PROGRAM  \xC2\xB7  RUNS AT THE PROGRAM'S TEMPO", {x + 300, y + 11, w - 316, 12});
      // The top row: on, latch, style, speed, gate, octaves, length.
      name("ARP", {x + 16, y + 44, 110, 12});
      toggle(130, "ARP ON", kOrange, {x + 16, y + 60, 110, 28}, "Turns the arpeggiator on (NRPN 130).");
      name("LATCH", {x + 142, y + 44, 110, 12});
      toggle(131, "LATCH", kOrange, {x + 142, y + 60, 110, 28},
             "Keeps the notes playing after the keys are let go (NRPN 131).");
      name("STYLE", {x + 268, y + 44, 200, 12});
      setting(114, kOrange, {x + 268, y + 60, 200, 28}, "The order the held notes are played in (NRPN 114).");
      name("SPEED", {x + 484, y + 44, 140, 12});
      setting(116, kOrange, {x + 484, y + 60, 140, 28}, "Each step's length, at the program's tempo (NRPN 116).");
      name("GATE", {x + 640, y + 44, 150, 12});
      number(115, kOrange, {x + 640, y + 60, 150, 28}, "How long each step sounds, 0-127 (NRPN 115).");
      name("OCTAVES", {x + 806, y + 44, 150, 12});
      number(132, kOrange, {x + 806, y + 60, 150, 28}, "How many octaves the pattern climbs (NRPN 132).");
      name("LENGTH", {x + 972, y + 44, 150, 12});
      number(133, kOrange, {x + 972, y + 60, 150, 28},
             "Steps in the pattern, 1-16 (NRPN 133). Steps past it are skipped.");

      // The pattern, then a pad per step under its column: its number and what it plays.
      const Rect pattern{x + 16, y + 106, w - 32, 384};
      picture<vis::ArpPattern>(pattern, kOrange, static_cast<int>(kSettingIdBase));
      const std::vector<Rect> cols = ui::row({pattern.x, pattern.y + pattern.h + 12, pattern.w, 132}, 16, 6.0f);
      for (int i = 0; i < 16; ++i) {
         const Rect c = cols[static_cast<size_t>(i)];
         add<vis::Tile>(c);
         stepNumbers_[i] = &label(std::to_string(i + 1), {c.x, c.y + 14, c.w, 40}, kOrange, visage::Font::kCenter, 0.7f);
         name(i % 4 == 0 ? "BEAT " + std::to_string(i / 4 + 1) : "", {c.x, c.y + 60, c.w, 12}, visage::Font::kCenter);
         steps_[i] = &setting(134 + i, kOrange, {c.x + 6, c.y + c.h - 34, c.w - 12, 24},
                              "What this step plays (NRPN 134-149): AUTO follows the style, TIE holds the last note, "
                              "1ST-6TH a held note by order, < AND > the one before or after, CHORD all of them.");
      }
      hint("FAINT: AUTO, THE NEXT NOTE OF THE STYLE  \xC2\xB7  BARS: TIES  \xC2\xB7  STACKS: CHORDS  \xC2\xB7  "
           "SHADED: PAST THE LENGTH  \xC2\xB7  FOUR NOTES HELD",
           {x + 16, y + h - 26, w - 32, 12}, visage::Font::kLeft);
   }

   void buildProgram() {
      const float x = kMargin, y = kBottomY, w = kProgramW, h = kBottomH;
      card("PROGRAM", kWhite, {x, y, w, h});
      const auto row = [&](float ry, const char *title, const char *about) {
         name(title, {x + 16, ry, w - 32, 12});
         hint(about, {x + 16, ry + 46, w - 32, 12}, visage::Font::kLeft);
      };
      row(y + 44, "NAME", "SENT AS NRPN 1-8. STORE THE PROGRAM ON THE SYNTH TO KEEP IT");
      name_ = &add<NameField>({x + 16, y + 60, w - 32, 28});
      name_->current = [this] {
         std::string n;
         for (int i = 0; i < kNameLength; ++i)
            n += static_cast<char>(std::lround(host_.paramValue(settingId(nameNrpn(i)))));
         return n.substr(0, n.find_last_not_of(' ') + 1);
      };
      name_->onCommit = [this](const std::string &typed) {
         for (int i = 0; i < kNameLength; ++i) {
            const char ch = i < static_cast<int>(typed.size()) ? typed[static_cast<size_t>(i)] : ' ';
            const clap_id id = settingId(nameNrpn(i));
            host_.beginEdit(id);
            host_.setParam(id, ch >= 32 && ch <= 126 ? ch : ' ');
            host_.endEdit(id);
         }
      };
      tips_.add(*name_, "The program's name on the synth (NRPN 1-8), up to 8 characters.");
      row(y + 120, "TEMPO (BPM)", "THE ARP, SYNCED LFOS AND THE SYNCED DELAY FOLLOW IT");
      number(18, kWhite, {x + 16, y + 136, w - 32, 28}, "The program's tempo, 40 to 295 BPM (NRPN 18).");
      row(y + 196, "PROGRAM VOLUME", "SAVED IN THE PROGRAM, UNLIKE VOLUME (CC 7)");
      number(17, kWhite, {x + 16, y + 212, w - 32, 28}, "The program's own level, -24 to +24 (NRPN 17).");
      row(y + 272, "CATEGORY", "WHERE THE SYNTH FILES IT");
      setting(9, kWhite, {x + 16, y + 288, w - 32, 28}, "The program's category on the synth (NRPN 9).");
   }

   // What the synth's own F, Q and M knobs and its four encoders do in this program.
   void buildPanelKnobs() {
      const float x = kMargin + kProgramW + kGutter, y = kBottomY, w = kInner - kProgramW - kGutter, h = kBottomH;
      card("THE SYNTH'S KNOBS", kOrange, {x, y, w, h});
      hint("WHAT EACH KNOB ON THE SYNTH CONTROLS  \xC2\xB7  SYSTEM: THE SYNTH'S GLOBAL CHOICE", {x + 240, y + 11, w - 256, 12});
      const char *names[] = {"F KNOB", "Q KNOB", "M KNOB", "ENCODER 1", "ENCODER 2", "ENCODER 3", "ENCODER 4"};
      const std::vector<Rect> cols = ui::row({x + 16, y + 40, w - 32, h - 56}, 7, 12.0f);
      for (int i = 0; i < 7; ++i) {
         const Rect t = cols[static_cast<size_t>(i)];
         const unsigned c = i < 3 ? kOrange : kWhite;
         add<vis::Tile>(t);
         label(names[i], {t.x, t.y + 10, t.w, 16}, c, visage::Font::kCenter, 0.8f);
         add<vis::KnobGlyph>({t.x + t.w * 0.5f - 50, t.y + 40, 100, 100}, i >= 3);
         setting(10 + i, c, {t.x + 8, t.y + 164, t.w - 16, 26},
                 "What this knob on the synth controls in the program (NRPN 10-16).");
         hint("NRPN " + std::to_string(10 + i), {t.x + 8, t.y + 200, t.w - 16, 12}, visage::Font::kCenter);
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
      wheelValue_->setText(std::to_string(toCcValue(value(1))));
      frontWheel_->setText(std::to_string(toCcValue(value(1))));
      for (int i = 0; i < 3; ++i)
         syncHints_[i]->setText(std::lround(value(settingId(62 + 3 * i))) > 0
                                   ? "SYNCED"
                                   : "");
      const long length = std::lround(value(settingId(133))) + 1;
      for (int i = 0; i < 16; ++i) {
         steps_[i]->setAccent(i < length ? kOrange : 0xff4b515a);
         stepNumbers_[i]->setColor(i < length ? kOrange : 0xff4b515a);
      }

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
   ui::Label *wheelValue_ = nullptr, *frontWheel_ = nullptr;
   ui::Label *feeds_[5] = {}; // LFO 1-3, mod wheel, mod knob
   ui::Label *feedsOps_[3] = {}; // lag, mult 1, mult 2
   ui::Label *syncHints_[3] = {};
   ui::Dropdown *steps_[16] = {};
   ui::Label *stepNumbers_[16] = {};
   ui::TabBar *tabs_ = nullptr;
   NameField *name_ = nullptr;
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
