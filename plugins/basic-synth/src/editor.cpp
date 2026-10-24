// Basic Synth's panel, in the MicroMonsta 2 style: a title bar, the oscillator, filter, two envelopes,
// LFO and output, and a keyboard or MPE note grid to play it, on a 720 x 884 design grid scaled to the window.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/keyboard.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/octave_shift.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/scope.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>

#include <bitset>
#include <string>
#include <vector>

#include "params.h"

namespace basic_synth {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 884.0f;
constexpr float kHeaderHeight = 56.0f;
constexpr int kScopeSamples = 1024;

// The MicroMonsta 2 palette: orange for the oscillator and output, blue for the filter and Env 2, purple
// for Env 1, and a green for the LFO.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff8fd6a8;

enum PlayPage { kKeysPage, kGridPage };

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0, bool bipolar = false) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, bipolar};
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildOscillator();
      buildFilter();
      buildEnvelopes();
      buildLfo();
      buildOutput();
      buildPlay();
      startTimer(30);
   }
   // A key or cell still held with the mouse is let go, so closing the panel doesn't leave it hanging.
   ~Panel() override {
      keyboard_->releaseNote();
      grid_->releaseNote();
   }

   void showPage(int page) {
      tabs_->select(page);
      octave_->redraw();
   }

   void timerCallback() override {
      host_.voices(voices_);
      std::vector<ui::NoteGrid::Touch> touches;
      std::bitset<128> held;
      for (const EditorHost::Voice &v : voices_) {
         held.set(v.note);
         touches.push_back({v.note, v.bend, v.pressure, v.timbre});
      }
      // Every note, not just the ones shown, so the keyboard can point to notes off either end.
      for (int n = 0; n < 128; ++n)
         keyboard_->setNoteOn(n, held[n]);
      grid_->setTouches(touches);
      // Bright at the top of the LFO's swing, dark at the bottom, off with no note held.
      float lfo = 0.0f;
      const bool sounding = host_.lfo(lfo);
      lfoLight_->setLevel(sounding ? 0.5f + 0.5f * lfo : 0.0f);
      // Where the LFO has the cutoff and pulse width right now, as a green arc on from the host's
      // white one. The cutoff knob runs in log steps of kCutoffSpan, so octaves are scaled to it.
      const double swing = sounding ? lfo : 0.0;
      cutoff_->setLiveModulation(swing * host_.paramModulated(kLfoToCutoff) / std::log2(kCutoffSpan), kGreen);
      pulseWidth_->setLiveModulation(swing * host_.paramModulated(kLfoToPulseWidth) * kLfoWidthSwing, kGreen);
      // A glowing dot on each envelope for every sounding voice.
      for (int i = 0; i < 2; ++i) {
         host_.envelopes(i, envStates_);
         std::vector<ui::EnvelopeEditor::Playhead> heads;
         for (const EditorHost::EnvState &e : envStates_)
            heads.push_back({static_cast<ui::EnvelopeEditor::Stage>(e.stage), e.level, e.releaseFrom});
         envelopes_[i]->setPlayheads(std::move(heads));
      }
      host_.readScope(scopeSamples_.data(), scopeSamples_.size());
      scope_->setSamples(scopeSamples_);
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
      canvas.text("Basic", ui::font(26 * s), visage::Font::kLeft, 60 * s, 8 * s, 80 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("Synth", ui::font(26 * s), visage::Font::kLeft, 128 * s, 8 * s, 90 * s, 40 * s);
      canvas.setColor(t.dimText);
      canvas.text("8 voices  ·  MPE  ·  built on jclap::dsp", ui::font(13 * s), visage::Font::kRight, 360 * s,
                  8 * s, 340 * s, 40 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // One cycle of a saw: a ramp up, then the drop.
      canvas.setColor(kOrange);
      canvas.segment(18 * s, 40 * s, 40 * s, 16 * s, 2.5f * s, true);
      canvas.segment(40 * s, 16 * s, 40 * s, 40 * s, 2.5f * s, true);
   }

   ui::Knob &knob(clap_id id, const char *label, unsigned accent, ui::Rect cell, bool bipolar = false) {
      auto &k = add<ui::Knob>(cell, rangeOf(id, 0.0, bipolar));
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      return k;
   }

   ui::SegmentedSwitch &segments(clap_id id, const char *label, std::vector<std::string> names, unsigned accent,
                                 ui::Rect bounds) {
      add<ui::Label>({bounds.x, bounds.y - 18, bounds.w, 16}, label, accent, visage::Font::kLeft);
      auto &s = add<ui::SegmentedSwitch>(bounds, std::move(names));
      s.setAccent(accent);
      bindings_.bind(s, id);
      return s;
   }

   void buildOscillator() {
      add<ui::Section>({12, 68, 232, 196}, "OSCILLATOR");
      auto &dual = add<ui::Button>({172, 74, 60, 22}, "DUAL");
      dual.setAccent(kOrange);
      bindings_.bind(dual, kDual);
      tips_.add(dual, "A second oscillator, detuned from the first by DETUNE.");
      segments(kWave, "WAVE", {"SIN", "TRI", "SAW", "SQR", "PULSE"}, kOrange, {24, 116, 208, 26});
      const auto cells = ui::row({24, 152, 208, 104}, 3);
      pulseWidth_ = &knob(kPulseWidth, "WIDTH", kOrange, cells[0]);
      tips_.add(*pulseWidth_, "Pulse width, for the PULSE wave.");
      knob(kTune, "TUNE", kOrange, cells[1], true);
      tips_.add(knob(kDetune, "DETUNE", kOrange, cells[2], true),
                "The second oscillator's pitch, with DUAL on: cents near the middle, an octave at the ends.");
   }

   void buildFilter() {
      add<ui::Section>({256, 68, 452, 196}, "FILTER");
      segments(kFilterMode, "MODE", {"LP", "HP", "BP"}, kBlue, {268, 116, 150, 26});
      segments(kSlope, "SLOPE", {"12", "24", "36", "48 dB"}, kBlue, {430, 116, 266, 26});
      const auto cells = ui::row({268, 152, 428, 104}, 6);
      cutoff_ = &knob(kCutoff, "CUTOFF", kBlue, cells[0]);
      knob(kResonance, "RESO", kBlue, cells[1]);
      tips_.add(knob(kKeyTrack, "KEY TRACK", kBlue, cells[2]),
                "How far the cutoff follows the note: at 100% an octave up for each octave above C4.");
      tips_.add(knob(kPressureAmount, "PRESSURE", kBlue, cells[3]), "How far MPE pressure opens the cutoff.");
      tips_.add(knob(kTimbreAmount, "TIMBRE", kBlue, cells[4]),
                "How far MPE timbre (slide, CC 74) moves the cutoff either way.");
      tips_.add(knob(kVelocityToFilter, "VELOCITY", kBlue, cells[5]),
                "How far velocity opens the cutoff: the full amount at the hardest note.");
   }

   // An envelope: its editor, a loop toggle, and how much it sends to the filter, VCA and pitch.
   void envelope(int index, unsigned accent, float x) {
      const auto id = [index](ParamId env1Param) { return envParam(index, env1Param); };
      add<ui::Section>({x, 276, 346, 210}, index == 0 ? "ENV 1" : "ENV 2");
      auto &env = *(envelopes_[index] = &add<ui::EnvelopeEditor>({x + 12, 302, 322, 96}, rangeOf(id(kEnv1Attack), 1.0),
                                         rangeOf(id(kEnv1Decay), 1.0), rangeOf(id(kEnv1Sustain), 0.01),
                                         rangeOf(id(kEnv1Release), 1.0)));
      env.setAccent(accent);
      for (int stage = 0; stage < ui::EnvelopeEditor::kStageCount; ++stage) {
         const clap_id p = id(kEnv1Attack) + stage;
         env.stage(static_cast<ui::EnvelopeEditor::Stage>(stage)).valueText = [p](double v) { return text(p, v); };
      }
      bindings_.bind(env, id(kEnv1Attack), id(kEnv1Decay), id(kEnv1Sustain), id(kEnv1Release));

      auto &loop = add<ui::Button>({x + 12, 422, 62, 30}, "LOOP");
      loop.setAccent(accent);
      bindings_.bind(loop, id(kEnv1Loop));
      tips_.add(loop, "Loop: attack and decay repeat while the key is held.");

      const auto cells = ui::row({x + 80, 404, 254, 78}, 3);
      tips_.add(knob(id(kEnv1ToFilter), "FILTER", accent, cells[0], true), "How far it moves the cutoff, either way.");
      tips_.add(knob(id(kEnv1ToVca), "VCA", accent, cells[1]),
                "How much it shapes the VCA. Whatever the envelopes leave, the key opens and closes.");
      tips_.add(knob(id(kEnv1ToPitch), "PITCH", accent, cells[2], true), "How far it bends the pitch, either way.");
   }

   void buildEnvelopes() {
      envelope(0, kPurple, 12);
      envelope(1, kBlue, 362);
   }

   void buildLfo() {
      add<ui::Section>({12, 498, 346, 170}, "LFO");
      lfoLight_ = &add<ui::ActivityLed>({262, 504, 84, 22}, "NOW");
      lfoLight_->setColor(kGreen);
      lfoLight_->fadeMs = 0;
      tips_.add(*lfoLight_, "The LFO of the newest held note.");
      segments(kLfoShape, "SHAPE", {"SIN", "TRI", "SAW", "SQR", "RND"}, kGreen, {24, 544, 322, 26});
      const auto cells = ui::row({24, 576, 322, 88}, 4);
      knob(kLfoRate, "RATE", kGreen, cells[0]);
      knob(kLfoToCutoff, "TO CUTOFF", kGreen, cells[1], true);
      knob(kLfoToVca, "TO VCA", kGreen, cells[2]);
      tips_.add(knob(kLfoToPulseWidth, "TO WIDTH", kGreen, cells[3]),
                "How far it sweeps the pulse width, around the WIDTH knob, for the PULSE wave.");
   }

   void buildOutput() {
      const unsigned dim = ui::defaultTheme().dimText;
      add<ui::Section>({362, 498, 346, 170}, "OUTPUT");
      knob(kLevel, "LEVEL", kOrange, {372, 526, 80, 136});
      tips_.add(knob(kVelocityToVca, "VELOCITY", kOrange, {456, 526, 80, 136}),
                "How much velocity sets the loudness: at 0 every note plays as loud as the hardest.");
      add<ui::Label>({546, 526, 150, 16}, "MPE BEND RANGE", dim, visage::Font::kLeft);
      auto &bend = add<ui::NumberBox>({546, 544, 150, 28}, rangeOf(kBendRange, 1.0));
      bend.setStepButtons(true);
      bend.valueText = [](double v) { return text(kBendRange, v); };
      bend.parseText = [](const std::string &t, double &v) { return parseParam(kBendRange, t.c_str(), v); };
      bindings_.bind(bend, kBendRange);
      scope_ = &add<ui::Scope>({546, 582, 150, 78});
      scope_->setAccent(kOrange);
   }

   void buildPlay() {
      add<ui::Section>({12, 680, 696, 192}, "PLAY");
      tabs_ = &add<ui::TabBar>({508, 684, 188, 24}, std::vector<std::string>{"KEYS", "GRID"});
      keyboard_ = &tabs_->addToPage(kKeysPage, add<ui::PianoKeyboard>({24, 716, 672, 144}, 36, 84));
      keyboard_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      keyboard_->onNoteOff = [this](int note) { host_.noteOff(note); };
      // Press a cell, slide sideways to bend, up and down for timbre. An MPE controller's notes show
      // their bend, pressure and timbre here as they play.
      grid_ = &tabs_->addToPage(kGridPage, add<ui::NoteGrid>({24, 716, 672, 144}, 16, 5, 40));
      grid_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      grid_->onExpression = [this](int note, float bend, float pressure, float timbre) {
         host_.noteExpression(note, bend, pressure, timbre);
      };
      grid_->onNoteOff = [this](int note) { host_.noteOff(note); };
      tabs_->select(kKeysPage);
      tabs_->onChange = [this](int) { octave_->redraw(); };

      // Octaves up and down, for the keyboard and grid together. The readout shows the range of
      // whichever is showing.
      octave_ = &add<ui::OctaveShift>({300, 684, 196, 24}, -3, 3);
      octave_->setAccent(kOrange);
      octave_->valueText = [this](int) {
         const bool keys = tabs_->selected() == kKeysPage;
         const int low = keys ? keyboard_->lowNote() : grid_->lowNote();
         const int high = std::min(127, keys ? keyboard_->highNote() : grid_->highNote());
         return ui::noteName(low) + " - " + ui::noteName(high);
      };
      octave_->onChange = [this](int value, int step) {
         keyboard_->shiftOctaves(step);
         grid_->shiftOctaves(step);
         host_.playOctave = value;
      };
      tips_.add(*octave_, "Moves the keyboard and grid an octave down or up.");
      keyboard_->shiftOctaves(host_.playOctave);
      grid_->shiftOctaves(host_.playOctave);
      octave_->setValue(host_.playOctave);

      // The grid's rows: fourths like a LinnStrument, fifths, or octaves for the widest range.
      auto &rows = tabs_->addToPage(kGridPage, add<ui::SegmentedSwitch>({100, 684, 180, 24},
                                                                       std::vector<std::string>{"4THS", "5THS", "OCT"}));
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
   ui::TabBar *tabs_ = nullptr;
   ui::PianoKeyboard *keyboard_ = nullptr;
   ui::NoteGrid *grid_ = nullptr;
   ui::Scope *scope_ = nullptr;
   ui::OctaveShift *octave_ = nullptr;
   ui::ActivityLed *lfoLight_ = nullptr;
   ui::Knob *cutoff_ = nullptr, *pulseWidth_ = nullptr;
   ui::EnvelopeEditor *envelopes_[2] = {};
   std::vector<EditorHost::EnvState> envStates_;
   std::vector<EditorHost::Voice> voices_;
   std::vector<float> scopeSamples_ = std::vector<float>(kScopeSamples);
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.showPage(page);
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace basic_synth
