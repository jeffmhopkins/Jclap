// JSynth's panel, on a 1280 x 940 design grid scaled to the window. The header switches between three
// pages and between the two parts:
//
//    MAIN  the selected part's voice: three oscillators, the FM matrix and mixer, drive, two filters
//          and their routing, the shaper, three envelopes and two LFOs
//    MOD   the selected part's 16 matrix slots, beside the touch response and the macros
//    PLAY  how notes reach the parts, each part's voice settings side by side, and a keyboard or
//          MPE note grid
//
// Both parts have their own widgets, built from the same code with the part's param ids, and the
// panel shows the ones for the selected page and part.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/keyboard.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/octave_shift.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <string>
#include <vector>

#include "params.h"

namespace jsynth {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 940.0f;
constexpr float kHeaderHeight = 56.0f;

// Basic Synth's palette, one colour per kind of module, plus one per part for the header and the
// keyboard: orange for part A and the oscillators, blue for part B and the filters.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff8fd6a8;
constexpr unsigned kAmber = 0xfff2c94c;
constexpr unsigned kRed = 0xffef7b6c;
constexpr unsigned kTeal = 0xff6fd3d0;
constexpr unsigned kPartColors[kPartCount] = {kOrange, kBlue};

enum Page { kMainPage, kModPage, kPlayPage, kPageCount };
enum PlayView { kKeysView, kGridView };
// A widget shown on every part's version of its page.
constexpr int kAnyPart = -1;

std::string text(clap_id id, double value) {
   char buffer[48];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id) {
   const Param &p = *findParam(id);
   const bool stepped = p.def.flags & CLAP_PARAM_IS_STEPPED;
   const bool bipolar = p.unit == Unit::Bipolar || p.unit == Unit::Semitones || p.unit == Unit::Cents ||
                        p.unit == Unit::Curve;
   return {p.def.min, p.def.max, p.def.defaultValue, stepped ? 1.0 : 0.0, bipolar};
}

std::vector<std::string> upper(const std::vector<std::string> &names) {
   std::vector<std::string> out;
   for (std::string s : names) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      out.push_back(s);
   }
   return out;
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildHeader();
      for (int part = 0; part < kPartCount; ++part) {
         at(kMainPage, part);
         buildOscillators(part);
         buildFm(part);
         buildMixer(part);
         buildFilters(part);
         buildEnvelopes(part);
         buildLfos(part);
         at(kModPage, part);
         buildMatrix(part);
         at(kPlayPage, part);
         buildPartSettings(part);
      }
      at(kModPage, kAnyPart);
      buildExpression();
      buildMacros();
      at(kPlayPage, kAnyPart);
      buildPerformance();
      buildKeys();
      show(host_.page, host_.part);
      startTimer(30);
   }
   // A key or cell still held with the mouse is let go, so closing the panel doesn't leave it hanging.
   ~Panel() override {
      keyboard_->releaseNote();
      grid_->releaseNote();
   }

   void show(int page, int part) {
      host_.page = page = std::clamp(page, 0, kPageCount - 1);
      host_.part = part = std::clamp(part, 0, kPartCount - 1);
      // The play page shows both parts side by side.
      for (const Placed &p : placed_)
         p.frame->setVisible(p.page == page && (p.part == kAnyPart || p.part == part || page == kPlayPage));
      pageTabs_->select(page);
      partSwitch_->setSelected(part);
      partSwitch_->setVisible(page != kPlayPage);
      partSwitch_->setAccent(kPartColors[part]);
      showPlayView(playTabs_->selected());
      redraw();
   }
   // The keyboard or the note grid, while the play page is up.
   void showPlayView(int view) {
      playTabs_->select(view);
      const bool play = host_.page == kPlayPage;
      keyboard_->setVisible(play && view == kKeysView);
      grid_->setVisible(play && view == kGridView);
      gridRows_->setVisible(play && view == kGridView);
      octave_->redraw();
   }

   void timerCallback() override {
      host_.notes(notes_);
      std::vector<ui::NoteGrid::Touch> touches;
      std::bitset<128> held;
      for (const EditorHost::Note &n : notes_) {
         held.set(n.note);
         touches.push_back({n.note, n.bend, n.pressure, n.timbre});
      }
      // Every note, not just the ones shown, so the keyboard can point to notes off either end.
      for (int n = 0; n < 128; ++n)
         keyboard_->setNoteOn(n, held[n]);
      grid_->setTouches(touches);
      // The newest note's touch, as each dimension's light. Glide shows how far it is bent, either way.
      float touch[kTouchCount] = {};
      if (!notes_.empty()) {
         const EditorHost::Note &n = notes_.back();
         touch[0] = n.strike;
         touch[1] = n.pressure;
         touch[2] = std::min(1.0f, std::fabs(n.bend) / 12.0f);
         touch[3] = n.timbre;
         touch[4] = n.lift;
      }
      for (int i = 0; i < kTouchCount; ++i)
         touchLights_[i]->setLevel(touch[i]);
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      canvas.setColor(t.background);
      canvas.fill(0, 0, width(), height());

      // Title bar: three stacked waves for the three oscillators, the name, and the part being edited.
      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kOrange);
      canvas.text("J", ui::font(26 * s), visage::Font::kLeft, 64 * s, 8 * s, 30 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("Synth", ui::font(26 * s), visage::Font::kLeft, 79 * s, 8 * s, 90 * s, 40 * s);
      if (host_.page != kPlayPage) {
         canvas.setColor(kPartColors[host_.part]);
         canvas.text(host_.part == 0 ? "EDITING PART A" : "EDITING PART B", ui::font(12 * s), visage::Font::kLeft,
                     640 * s, 8 * s, 140 * s, 40 * s);
      }
   }

private:
   struct Placed {
      visage::Frame *frame;
      int page, part;
   };

   void drawLogo(visage::Canvas &canvas, float s) {
      // Three saw teeth, offset like three detuned oscillators.
      for (int i = 0; i < 3; ++i) {
         canvas.setColor(i == 0 ? kOrange : i == 1 ? kAmber : kBlue);
         const float x = (14 + 4 * i) * s, y = (14 + 5 * i) * s;
         canvas.segment(x, y + 22 * s, x + 26 * s, y, 2.0f * s, true);
         canvas.segment(x + 26 * s, y, x + 26 * s, y + 22 * s, 2.0f * s, true);
      }
   }

   // The page and part the next widgets belong to.
   void at(int page, int part) {
      page_ = page;
      part_ = part;
   }
   template <typename T, typename... Args>
   T &put(ui::Rect bounds, Args &&...args) {
      T &frame = add<T>(bounds, std::forward<Args>(args)...);
      placed_.push_back({&frame, page_, part_});
      return frame;
   }
   // The host's id for a part param of the part being built.
   clap_id pid(clap_id offset) const { return partId(part_ < 0 ? 0 : part_, offset); }

   void section(ui::Rect bounds, const char *title) { put<ui::Section>(bounds, title); }
   void label(ui::Rect bounds, const std::string &textValue, unsigned color,
              visage::Font::Justification justify = visage::Font::kLeft) {
      put<ui::Label>(bounds, textValue, color, justify);
   }

   ui::Knob &knob(clap_id id, const char *name, unsigned accent, ui::Rect cell, const char *tip = nullptr) {
      auto &k = put<ui::Knob>(cell, rangeOf(id));
      k.setLabel(name);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      if (tip)
         tips_.add(k, tip);
      return k;
   }

   ui::Dropdown &dropdown(clap_id id, unsigned accent, ui::Rect bounds, const char *tip = nullptr) {
      auto &d = put<ui::Dropdown>(bounds, upper(*findParam(id)->choices));
      d.setAccent(accent);
      bindings_.bind(d, id);
      if (tip)
         tips_.add(d, tip);
      return d;
   }

   ui::SegmentedSwitch &segments(clap_id id, std::vector<std::string> names, unsigned accent, ui::Rect bounds,
                                 const char *title = nullptr, const char *tip = nullptr) {
      if (title)
         label({bounds.x, bounds.y - 18, bounds.w, 16}, title, accent);
      auto &s = put<ui::SegmentedSwitch>(bounds, std::move(names));
      s.setAccent(accent);
      bindings_.bind(s, id);
      if (tip)
         tips_.add(s, tip);
      return s;
   }

   ui::Button &toggle(clap_id id, const char *name, unsigned accent, ui::Rect bounds, const char *tip = nullptr) {
      auto &b = put<ui::Button>(bounds, name);
      b.setAccent(accent);
      bindings_.bind(b, id);
      if (tip)
         tips_.add(b, tip);
      return b;
   }

   ui::NumberBox &numberBox(clap_id id, const char *title, unsigned color, ui::Rect bounds, const char *tip = nullptr) {
      label({bounds.x, bounds.y - 18, bounds.w, 16}, title, color);
      auto &n = put<ui::NumberBox>(bounds, rangeOf(id));
      n.setStepButtons(true);
      n.valueText = [id](double v) { return text(id, v); };
      n.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(n, id);
      if (tip)
         tips_.add(n, tip);
      return n;
   }

   // ---- header ----

   void buildHeader() {
      pageTabs_ = &add<ui::TabBar>({220, 16, 300, 26}, std::vector<std::string>{"MAIN", "MOD", "PLAY"});
      pageTabs_->onChange = [this](int page) {
         if (page != host_.page)
            show(page, host_.part);
      };
      partSwitch_ = &add<ui::SegmentedSwitch>({540, 16, 90, 26}, std::vector<std::string>{"A", "B"});
      partSwitch_->setAccent(kOrange);
      partSwitch_->onChange = [this](int part) { show(host_.page, part); };
      tips_.add(*partSwitch_, "The part the MAIN and MOD pages edit.");
      auto &mode = add<ui::Dropdown>({880, 16, 170, 26}, upper(kPlayModeNames));
      mode.setAccent(kOrange);
      bindings_.bind(mode, kPlayMode);
      tips_.add(mode, "Which part plays each note: A only, both, split by key, by MPE zone, by velocity, or "
                      "crossfaded by touch.");
      add<ui::Label>({800, 16, 74, 26}, "MODE", ui::defaultTheme().dimText, visage::Font::kRight, 0.5f);
      auto &quality = add<ui::SegmentedSwitch>({1130, 16, 138, 26}, upper(kQualityNames));
      quality.setAccent(kOrange);
      bindings_.bind(quality, kQuality);
      tips_.add(quality, "Oversampling for the oscillators, FM and filters: more is cleaner and costs more CPU.");
      add<ui::Label>({1060, 16, 64, 26}, "QUALITY", ui::defaultTheme().dimText, visage::Font::kRight, 0.5f);
   }

   // ---- main page ----

   void buildOscillators(int) {
      for (int o = 0; o < kOscCount; ++o) {
         const float x = 12.0f + o * 270.0f, y = 68.0f;
         const auto id = [&](OscParam p) { return pid(oscParam(o, p)); };
         section({x, y, 262, 300}, o == 0 ? "OSC 1" : o == 1 ? "OSC 2" : "OSC 3");
         if (o > 0)
            toggle(id(kOscSync), "SYNC", kOrange, {x + 188, y + 6, 62, 22}, "Hard sync to Osc 1.");
         label({x + 12, y + 34, 60, 16}, "WAVE", kOrange);
         dropdown(id(kOscWave), kOrange, {x + 12, y + 52, 142, 26},
                  "Analog waves, the TX81Z's eight FM operator waves, or noise.");
         toggle(id(kOscRetrigger), "RETRIG", kOrange, {x + 162, y + 52, 88, 26},
                "Restart the wave at PHASE with each note, or let it run free.");
         segments(id(kOscPitchMode), {"NOTE", "RATIO", "FIXED"}, kOrange, {x + 12, y + 104, 238, 24}, "PITCH",
                  "NOTE follows the key in semitones, RATIO is a multiple of the note (for FM), FIXED ignores the key.");
         const auto row1 = ui::row({x + 8, y + 134, 246, 80}, 4);
         knob(id(kOscCoarse), "COARSE", kOrange, row1[0]);
         knob(id(kOscFine), "FINE", kOrange, row1[1]);
         knob(id(kOscRatio), "RATIO", kOrange, row1[2], "The note times this, in RATIO mode.");
         knob(id(kOscFixed), "FIXED", kOrange, row1[3], "The pitch in FIXED mode.");
         const auto row2 = ui::row({x + 8, y + 216, 246, 80}, 4);
         knob(id(kOscShape), "SHAPE", kOrange, row2[0], "Pulse width, or how far each wave is skewed.");
         knob(id(kOscPhase), "PHASE", kOrange, row2[1]);
         knob(id(kOscBend), "BEND", kOrange, row2[2], "How much of each note's MPE bend this oscillator follows.");
         toggle(id(kOscKeyTrack), "KEY", kOrange, {row2[3].x + 8, row2[3].y + 26, row2[3].w - 16, 26},
                "Follow the key. Off, the oscillator stays at C4's pitch whatever you play.");
      }
   }

   void buildFm(int) {
      const float x = 822, y = 68;
      section({x, y, 176, 300}, "FM");
      label({x + 12, y + 30, 156, 14}, "ROW MODULATES COLUMN", ui::defaultTheme().dimText);
      for (int to = 0; to < kOscCount; ++to)
         label({x + 28 + to * 48.0f, y + 46, 46, 14}, std::to_string(to + 1), kAmber, visage::Font::kCenter);
      for (int from = 0; from < kOscCount; ++from) {
         label({x + 6, y + 84 + from * 64.0f, 20, 16}, std::to_string(from + 1), kAmber, visage::Font::kCenter);
         for (int to = 0; to < kOscCount; ++to) {
            const clap_id id = pid(fmCell(from, to));
            knob(id, from == to ? "FB" : "", kAmber, {x + 28 + to * 48.0f, y + 62 + from * 64.0f, 46, 62},
                 from == to ? "Feedback: the oscillator modulates its own phase." : nullptr);
         }
      }
      segments(pid(kFmBase + kFmType), {"PHASE", "EXP"}, kAmber, {x + 12, y + 266, 152, 24}, nullptr,
               "PHASE is linear phase modulation, DX style, which stays in tune at any depth. EXP is analog-style "
               "exponential FM, which drifts sharp as it deepens.");
   }

   // A channel strip per source: a level fader, then pan (or the ring's pair, or the noise's colour)
   // and the filter it goes to.
   void buildMixer(int) {
      const float x = 1006, y = 68;
      section({x, y, 262, 300}, "MIXER");
      const char *const names[5] = {"OSC 1", "OSC 2", "OSC 3", "RING", "NOISE"};
      for (int c = 0; c < 5; ++c) {
         const float cx = x + 8 + c * 49.6f;
         clap_id level, middle, route;
         if (c < kOscCount) {
            level = mixParam(kMixLevel, c);
            middle = mixParam(kMixPan, c);
            route = mixParam(kMixRoute, c);
         } else if (c == 3) {
            level = mixParam(kRingLevel);
            middle = mixParam(kRingPair);
            route = mixParam(kRingRoute);
         } else {
            level = mixParam(kNoiseLevel);
            middle = mixParam(kNoiseColor);
            route = mixParam(kNoiseRoute);
         }
         auto &fader = put<ui::Slider>({cx, y + 30, 48, 136}, ui::Slider::Orientation::Vertical, rangeOf(pid(level)));
         fader.setLabel(names[c]);
         fader.setAccent(kOrange);
         const clap_id levelId = pid(level);
         fader.valueText = [levelId](double v) { return text(levelId, v); };
         fader.parseText = [levelId](const std::string &t, double &v) { return parseParam(levelId, t.c_str(), v); };
         bindings_.bind(fader, levelId);
         if (c == 3) {
            label({cx, y + 172, 48, 14}, "PAIR", ui::defaultTheme().dimText, visage::Font::kCenter);
            dropdown(pid(middle), kOrange, {cx, y + 190, 48, 24}, "Which two oscillators the ring modulator multiplies.");
         } else {
            knob(pid(middle), c == 4 ? "COLOR" : "PAN", kOrange, {cx, y + 170, 48, 60}, c == 4 ? "Noise colour: dark to bright." : nullptr);
         }
         knob(pid(route), "ROUTE", kOrange, {cx, y + 234, 48, 60},
              c == 0 ? "With the filters split, which filter it goes to: Filter 1 at -100%, Filter 2 at +100%." : nullptr);
      }
   }

   void buildFilters(int) {
      const float y = 376;
      // Drive, before the filters.
      section({12, y, 150, 220}, "DRIVE");
      segments(pid(tailParam(kDriveType)), {"SOFT", "HARD", "TUBE"}, kRed, {24, y + 52, 126, 24}, "TYPE");
      knob(pid(tailParam(kDriveAmount)), "DRIVE", kRed, {24, y + 88, 126, 124}, "Saturation before the filters.");

      for (int f = 0; f < kFilterCount; ++f) {
         const float x = f == 0 ? 170.0f : 736.0f;
         const auto id = [&](FilterParam p) { return pid(filterParam(f, p)); };
         section({x, y, 400, 220}, f == 0 ? "FILTER 1" : "FILTER 2");
         label({x + 12, y + 34, 100, 16}, "TYPE", kBlue);
         dropdown(id(kFilterType), kBlue, {x + 12, y + 52, 150, 26});
         segments(id(kFilterSlope), {"12", "24", "36", "48 dB"}, kBlue, {x + 172, y + 52, 216, 26}, "SLOPE");
         const auto cells = ui::row({x + 8, y + 92, 384, 120}, 5);
         knob(id(kFilterCutoff), "CUTOFF", kBlue, cells[0]);
         knob(id(kFilterResonance), "RESO", kBlue, cells[1]);
         knob(id(kFilterDrive), "DRIVE", kBlue, cells[2], "The filter's own saturation, strongest on the ladder.");
         knob(id(kFilterKeyTrack), "KEY TRACK", kBlue, cells[3], "At 100% the cutoff moves an octave per octave from C4.");
         knob(id(kFilterFm), "FM", kBlue, cells[4], "Osc 3 moves the cutoff at audio rate.");
      }

      section({578, y, 150, 220}, "ROUTING");
      segments(pid(tailParam(kFilterRouting)), {"SER", "PAR", "SPLIT"}, kBlue, {590, y + 52, 126, 24}, nullptr,
               "SER: Filter 1 into Filter 2. PAR: both hear the mix. SPLIT: each source's ROUTE in the mixer picks one.");
      knob(pid(tailParam(kFilterBalance)), "BALANCE", kBlue, {590, y + 88, 126, 124},
           "Filter 1 against Filter 2, in parallel and split.");

      section({1144, y, 124, 220}, "SHAPER");
      dropdown(pid(tailParam(kShaperType)), kRed, {1154, y + 34, 104, 24}, "Waveshaping after the filters, per voice.");
      knob(pid(tailParam(kShaperAmount)), "AMOUNT", kRed, {1154, y + 64, 104, 76});
      knob(pid(tailParam(kShaperMix)), "MIX", kRed, {1154, y + 140, 104, 76});
   }

   void buildEnvelopes(int) {
      for (int e = 0; e < kEnvCount; ++e) {
         const float x = 12.0f + e * 308.0f, y = 604;
         const unsigned accent = e == 0 ? kPurple : e == 1 ? kBlue : kAmber;
         const auto id = [&](EnvParam p) { return pid(envParam(e, p)); };
         section({x, y, 300, 324}, e == 0 ? "ENV 1  -  AMP" : e == 1 ? "ENV 2" : "ENV 3");
         auto &env = put<ui::EnvelopeEditor>({x + 12, y + 30, 276, 104}, rangeOf(id(kEnvAttack)), rangeOf(id(kEnvDecay)),
                                             rangeOf(id(kEnvSustain)), rangeOf(id(kEnvRelease)));
         env.setAccent(accent);
         // The times are already on a curve (see timeMs), so draw them straight.
         env.setTimeCurve(1.0f);
         const clap_id stages[4] = {id(kEnvAttack), id(kEnvDecay), id(kEnvSustain), id(kEnvRelease)};
         for (int s = 0; s < ui::EnvelopeEditor::kStageCount; ++s) {
            const clap_id p = stages[s];
            env.stage(static_cast<ui::EnvelopeEditor::Stage>(s)).valueText = [p](double v) { return text(p, v); };
         }
         bindings_.bind(env, stages[0], stages[1], stages[2], stages[3]);
         segments(id(kEnvLoop), {"ONE SHOT", "LOOP HELD", "FREE"}, accent, {x + 12, y + 142, 276, 24}, nullptr,
                  "LOOP HELD repeats attack and decay while the key is down. FREE is one cycle every voice shares, "
                  "running from when it's switched on.");
         const auto row1 = ui::row({x + 8, y + 172, 284, 72}, 3);
         knob(id(kEnvDelay), "DELAY", accent, row1[0]);
         knob(id(kEnvHold), "HOLD", accent, row1[1]);
         knob(id(kEnvVelocity), "VELOCITY", accent, row1[2], "How much softer notes shrink the envelope.");
         const auto row2 = ui::row({x + 8, y + 246, 284, 72}, 3);
         knob(id(kEnvAttackCurve), "A CURVE", accent, row2[0]);
         knob(id(kEnvDecayCurve), "D CURVE", accent, row2[1]);
         knob(id(kEnvReleaseCurve), "R CURVE", accent, row2[2]);
      }
   }

   void buildLfos(int) {
      for (int l = 0; l < kLfoCount; ++l) {
         const float x = 936, y = 604.0f + l * 166.0f;
         const auto id = [&](LfoParam p) { return pid(lfoParam(l, p)); };
         section({x, y, 332, 158}, l == 0 ? "LFO 1" : "LFO 2");
         dropdown(id(kLfoShape), kGreen, {x + 12, y + 30, 110, 24});
         segments(id(kLfoTrigger), {"RETRIG", "FREE", "GLOBAL"}, kGreen, {x + 130, y + 30, 190, 24}, nullptr,
                  "RETRIG restarts with each note, FREE runs on in each voice, GLOBAL is one LFO the whole part shares.");
         const auto cells = ui::row({x + 8, y + 60, 228, 92}, 3);
         knob(id(kLfoRate), "RATE", kGreen, cells[0]);
         knob(id(kLfoPhase), "PHASE", kGreen, cells[1]);
         knob(id(kLfoDelay), "FADE IN", kGreen, cells[2]);
         toggle(id(kLfoSync), "SYNC", kGreen, {x + 242, y + 64, 78, 24}, "Lock the rate to the host's tempo.");
         dropdown(id(kLfoDivision), kGreen, {x + 242, y + 94, 78, 24});
         segments(id(kLfoPolarity), {"+/-", "+"}, kGreen, {x + 242, y + 124, 78, 24});
      }
   }

   // ---- mod page ----

   void buildMatrix(int) {
      const float x = 12, y = 68;
      const unsigned dim = ui::defaultTheme().dimText;
      section({x, y, 820, 860}, "MOD MATRIX");
      struct Column {
         const char *title;
         float x, w;
      };
      const Column cols[] = {{"", 24, 24},          {"SOURCE", 56, 140}, {"VIA", 204, 140},
                             {"CURVE", 352, 110},   {"AMOUNT", 470, 150}, {"DESTINATION", 628, 192}};
      for (const Column &c : cols)
         label({x + c.x, y + 34, c.w, 14}, c.title, dim);
      for (int s = 0; s < kSlotCount; ++s) {
         const float ry = y + 54 + s * 49.0f;
         const auto id = [&](SlotParam p) { return pid(slotParam(s, p)); };
         label({x + cols[0].x, ry + 9, cols[0].w, 18}, std::to_string(s + 1), kTeal, visage::Font::kCenter);
         dropdown(id(kSlotSource), kTeal, {x + cols[1].x, ry + 4, cols[1].w, 30});
         dropdown(id(kSlotVia), kTeal, {x + cols[2].x, ry + 4, cols[2].w, 30},
                  s == 0 ? "Scales the route by a second source: LFO 1 via Press is vibrato that grows as you press." : nullptr);
         dropdown(id(kSlotCurve), kTeal, {x + cols[3].x, ry + 4, cols[3].w, 30});
         auto &amount = put<ui::Slider>({x + cols[4].x, ry + 2, cols[4].w, 34}, ui::Slider::Orientation::Horizontal,
                                        rangeOf(id(kSlotAmount)));
         amount.setAccent(kTeal);
         const clap_id amountId = id(kSlotAmount);
         amount.valueText = [amountId](double v) { return text(amountId, v); };
         amount.parseText = [amountId](const std::string &t, double &v) { return parseParam(amountId, t.c_str(), v); };
         amount.setShowValuePopup(true);
         bindings_.bind(amount, amountId);
         dropdown(id(kSlotDestination), kTeal, {x + cols[5].x, ry + 4, cols[5].w - 12, 30});
      }
   }

   void buildExpression() {
      const float x = 840, y = 68;
      const unsigned dim = ui::defaultTheme().dimText;
      section({x, y, 428, 682}, "TOUCH");
      label({x + 12, y + 30, 404, 14}, "EACH DIMENSION'S RESPONSE, FOR BOTH PARTS", dim);
      const char *const what[kTouchCount] = {"Velocity", "Pressure", "Bend", "Y / CC 74", "Release velocity"};
      for (int i = 0; i < kTouchCount; ++i) {
         const float ry = y + 52 + i * 104.0f;
         label({x + 16, ry + 26, 120, 20}, upper({kTouchNames[i]})[0], kTeal);
         label({x + 16, ry + 48, 120, 16}, what[i], dim);
         auto &light = put<ui::ActivityLed>({x + 16, ry + 68, 110, 20}, "NOW");
         light.setColor(kTeal);
         light.fadeMs = 0;
         touchLights_[i] = &light;
         knob(kTouchCurve1 + i, "CURVE", kTeal, {x + 150, ry, 130, 100},
              "Bends the response: below 0 a light touch does more, above 0 it takes a firm one.");
         knob(kTouchSmooth1 + i, "SMOOTH", kTeal, {x + 286, ry, 130, 100});
      }
      const float ry = y + 572;
      label({x + 16, ry + 26, 120, 20}, "ROUNDING", kTeal);
      label({x + 16, ry + 48, 130, 16}, "Held notes settle in tune", dim);
      knob(kGlideRounding, "AMOUNT", kTeal, {x + 150, ry, 130, 100},
           "Pulls a resting finger to the nearest semitone. Slides stay free.");
      knob(kRoundingTime, "TIME", kTeal, {x + 286, ry, 130, 100});
   }

   void buildMacros() {
      const float x = 840, y = 758;
      section({x, y, 428, 170}, "MACROS");
      const auto cells = ui::row({x + 8, y + 34, 412, 128}, kMacroCount);
      for (int i = 0; i < kMacroCount; ++i)
         knob(kMacro1 + i, i == 0 ? "MACRO 1" : i == 1 ? "MACRO 2" : i == 2 ? "MACRO 3" : "MACRO 4", kTeal, cells[i]);
   }

   // ---- play page ----

   void buildPerformance() {
      const float y = 68;
      section({12, y, 1256, 140}, "PERFORMANCE");
      segments(kPlayMode, upper(kPlayModeNames), kOrange, {24, y + 56, 560, 28}, "PLAY MODE");
      numberBox(kSplitKey, "SPLIT KEY", kOrange, {600, y + 56, 120, 28}, "Split: part A plays below this key, B from it up.");
      numberBox(kVelocitySplit, "VELOCITY SPLIT", kOrange, {736, y + 56, 130, 28},
                "Velocity: part A plays below this velocity, B from it up.");
      segments(kCrossfadeSource, upper(kCrossfadeNames), kOrange, {882, y + 56, 130, 28}, "CROSSFADE",
               "Expression: what moves each note from part A to part B.");
      numberBox(kManagerBendRange, "MANAGER BEND", kOrange, {1028, y + 56, 120, 28},
                "How far the MPE manager channel's bend (or plain MIDI's) moves every note.");
      knob(kMasterLevel, "MASTER", kOrange, {1160, y + 26, 100, 110});
   }

   void buildPartSettings(int part) {
      const float x = part == 0 ? 12.0f : 644.0f, y = 216;
      const unsigned c = kPartColors[part];
      section({x, y, 624, 310}, part == 0 ? "PART A" : "PART B");
      const auto knobs = ui::row({x + 8, y + 30, 608, 100}, 7);
      knob(pid(kPartLevel), "LEVEL", c, knobs[0]);
      knob(pid(kPartPan), "PAN", c, knobs[1]);
      knob(pid(kPartTranspose), "TRANSPOSE", c, knobs[2]);
      knob(pid(kPartFine), "FINE", c, knobs[3]);
      knob(pid(kGlideTime), "GLIDE", c, knobs[4]);
      knob(pid(kDrift), "DRIFT", c, knobs[5], "Each voice's own slow wander in pitch and cutoff, like an analog synth.");
      knob(pid(kVelocityToAmp), "VELOCITY", c, knobs[6], "How much velocity sets each note's loudness.");
      segments(pid(kVoiceMode), upper(kVoiceModeNames), c, {x + 16, y + 160, 220, 26}, "VOICES");
      segments(pid(kGlideMode), upper(kGlideModeNames), c, {x + 252, y + 160, 170, 26}, "GLIDE",
               "LEGATO glides only between overlapping notes.");
      segments(pid(kPartOutput), {"MAIN", "OWN"}, c, {x + 438, y + 160, 170, 26}, "OUTPUT",
               "OWN sends this part to its own output pair, so the host can treat A and B apart.");
      numberBox(pid(kKeyLow), "KEY LOW", c, {x + 16, y + 232, 130, 28});
      numberBox(pid(kKeyHigh), "KEY HIGH", c, {x + 162, y + 232, 130, 28});
      numberBox(pid(kPolyphony), "POLYPHONY", c, {x + 308, y + 232, 130, 28}, "Notes this part plays at once, up to 16.");
      numberBox(pid(kBendRange), "MPE BEND RANGE", c, {x + 454, y + 232, 154, 28});
   }

   void buildKeys() {
      const float y = 534;
      section({12, y, 1256, 394}, "PLAY");
      playTabs_ = &add<ui::TabBar>({1068, y + 4, 188, 24}, std::vector<std::string>{"KEYS", "GRID"});
      placed_.push_back({playTabs_, kPlayPage, kAnyPart});
      keyboard_ = &add<ui::PianoKeyboard>({24, y + 36, 1232, 346}, 36, 96);
      keyboard_->setAccent(kOrange);
      keyboard_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      keyboard_->onNoteOff = [this](int note) { host_.noteOff(note); };
      // Press a cell, slide sideways to bend, up and down for timbre.
      grid_ = &add<ui::NoteGrid>({24, y + 36, 1232, 346}, 25, 8, 28);
      grid_->setAccent(kOrange);
      grid_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      grid_->onExpression = [this](int note, float bend, float pressure, float timbre) {
         host_.noteExpression(note, bend, pressure, timbre);
      };
      grid_->onNoteOff = [this](int note) { host_.noteOff(note); };
      playTabs_->onChange = [this](int view) { showPlayView(view); };

      octave_ = &put<ui::OctaveShift>({860, y + 4, 196, 24}, -3, 3);
      octave_->setAccent(kOrange);
      octave_->valueText = [this](int) {
         const bool keys = playTabs_->selected() == kKeysView;
         const int low = keys ? keyboard_->lowNote() : grid_->lowNote();
         const int high = std::min(127, keys ? keyboard_->highNote() : grid_->highNote());
         return ui::noteName(low) + " - " + ui::noteName(high);
      };
      octave_->onChange = [this](int value, int step) {
         keyboard_->shiftOctaves(step);
         grid_->shiftOctaves(step);
         host_.playOctave = value;
      };
      keyboard_->shiftOctaves(host_.playOctave);
      grid_->shiftOctaves(host_.playOctave);
      octave_->setValue(host_.playOctave);

      auto &rows = *(gridRows_ = &add<ui::SegmentedSwitch>({660, y + 4, 180, 24}, std::vector<std::string>{"4THS", "5THS", "OCT"}));
      rows.setAccent(kOrange);
      static constexpr int kIntervals[3] = {5, 7, 12};
      rows.setSelected(host_.gridInterval == 7 ? 1 : host_.gridInterval == 12 ? 2 : 0);
      grid_->setRowInterval(kIntervals[rows.selected()]);
      rows.onChange = [this](int index) {
         grid_->setRowInterval(kIntervals[index]);
         host_.gridInterval = kIntervals[index];
         octave_->redraw();
      };
      tips_.add(rows, "How far apart the grid's rows are.");
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   std::vector<Placed> placed_;
   int page_ = 0, part_ = 0;
   ui::TabBar *pageTabs_ = nullptr;
   ui::SegmentedSwitch *partSwitch_ = nullptr;
   ui::TabBar *playTabs_ = nullptr;
   ui::PianoKeyboard *keyboard_ = nullptr;
   ui::NoteGrid *grid_ = nullptr;
   ui::SegmentedSwitch *gridRows_ = nullptr;
   ui::OctaveShift *octave_ = nullptr;
   ui::ActivityLed *touchLights_[kTouchCount] = {};
   std::vector<EditorHost::Note> notes_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int page, int part, int playPage, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.show(page, part);
   panel.showPlayView(playPage);
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jsynth
