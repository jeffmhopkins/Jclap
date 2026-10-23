// Basic Synth's panel, in the MicroMonsta 2 style: a title bar, the oscillator, filter, envelope and
// output, and a keyboard or MPE note grid to play it, on a 720 x 640 design grid scaled to the window.

#include "editor.h"

#include <jclap/ui/design_frame.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/keyboard.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/scope.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>

#include <string>
#include <vector>

#include "params.h"

namespace basic_synth {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 640.0f;
constexpr float kHeaderHeight = 56.0f;
constexpr int kScopeSamples = 1024;

// The MicroMonsta 2 palette: orange for the oscillator, blue for the filter, purple for the envelope.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;

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
      buildEnvelope();
      buildOutput();
      buildPlay();
      startTimer(30);
   }

   void showPage(int page) { tabs_->select(page); }

   void timerCallback() override {
      host_.voices(voices_);
      std::vector<ui::NoteGrid::Touch> touches;
      for (int n = keyboard_->lowNote(); n <= keyboard_->highNote(); ++n)
         keyboard_->setNoteOn(n, false);
      for (const EditorHost::Voice &v : voices_) {
         keyboard_->setNoteOn(v.note, true);
         touches.push_back({v.note, v.bend, v.pressure, v.timbre});
      }
      grid_->setTouches(touches);
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
      segments(kWave, "WAVE", {"SIN", "TRI", "SAW", "SQR", "PULSE"}, kOrange, {24, 116, 208, 26});
      const auto cells = ui::row({24, 152, 208, 104}, 2);
      tips_.add(knob(kPulseWidth, "WIDTH", kOrange, cells[0]), "Pulse width, for the PULSE wave.");
      knob(kTune, "TUNE", kOrange, cells[1], true);
   }

   void buildFilter() {
      add<ui::Section>({256, 68, 452, 196}, "FILTER");
      segments(kFilterMode, "MODE", {"LP", "HP", "BP"}, kBlue, {268, 116, 150, 26});
      segments(kSlope, "SLOPE", {"12", "24", "36", "48 dB"}, kBlue, {430, 116, 266, 26});
      const auto cells = ui::row({268, 152, 428, 104}, 5);
      knob(kCutoff, "CUTOFF", kBlue, cells[0]);
      knob(kResonance, "RESO", kBlue, cells[1]);
      tips_.add(knob(kEnvAmount, "ENV", kBlue, cells[2], true), "How far the envelope moves the cutoff.");
      tips_.add(knob(kPressureAmount, "PRESSURE", kBlue, cells[3]), "How far MPE pressure opens the cutoff.");
      tips_.add(knob(kTimbreAmount, "TIMBRE", kBlue, cells[4]),
                "How far MPE timbre (slide, CC 74) moves the cutoff either way.");
   }

   void buildEnvelope() {
      add<ui::Section>({12, 276, 380, 148}, "ENVELOPE");
      auto &env = add<ui::EnvelopeEditor>({24, 302, 356, 114}, rangeOf(kAttack, 1.0), rangeOf(kDecay, 1.0),
                                         rangeOf(kSustain, 0.01), rangeOf(kRelease, 1.0));
      env.setAccent(kPurple);
      for (clap_id id : {kAttack, kDecay, kSustain, kRelease})
         env.stage(static_cast<ui::EnvelopeEditor::Stage>(id - kAttack)).valueText = [id](double v) { return text(id, v); };
      bindings_.bind(env, kAttack, kDecay, kSustain, kRelease);
      tips_.add(env, "Shapes the VCA and the filter. Drag the corners.");
   }

   void buildOutput() {
      const unsigned dim = ui::defaultTheme().dimText;
      add<ui::Section>({404, 276, 304, 148}, "OUTPUT");
      knob(kLevel, "LEVEL", kOrange, {414, 302, 84, 114});
      add<ui::Label>({508, 300, 188, 16}, "MPE BEND RANGE", dim, visage::Font::kLeft);
      auto &bend = add<ui::NumberBox>({508, 318, 188, 28}, rangeOf(kBendRange, 1.0));
      bend.setStepButtons(true);
      bend.valueText = [](double v) { return text(kBendRange, v); };
      bend.parseText = [](const std::string &t, double &v) { return parseParam(kBendRange, t.c_str(), v); };
      bindings_.bind(bend, kBendRange);
      scope_ = &add<ui::Scope>({508, 356, 188, 60});
      scope_->setAccent(kOrange);
   }

   void buildPlay() {
      add<ui::Section>({12, 436, 696, 192}, "PLAY");
      tabs_ = &add<ui::TabBar>({508, 440, 188, 24}, std::vector<std::string>{"KEYS", "GRID"});
      keyboard_ = &tabs_->addToPage(kKeysPage, add<ui::PianoKeyboard>({24, 472, 672, 144}, 36, 84));
      keyboard_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      keyboard_->onNoteOff = [this](int note) { host_.noteOff(note); };
      // Press a cell, slide sideways to bend, up and down for timbre. An MPE controller's notes show
      // their bend, pressure and timbre here as they play.
      grid_ = &tabs_->addToPage(kGridPage, add<ui::NoteGrid>({24, 472, 672, 144}, 16, 5, 40));
      grid_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      grid_->onExpression = [this](int note, float bend, float pressure, float timbre) {
         host_.noteExpression(note, bend, pressure, timbre);
      };
      grid_->onNoteOff = [this](int note) { host_.noteOff(note); };
      tabs_->select(kKeysPage);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::TabBar *tabs_ = nullptr;
   ui::PianoKeyboard *keyboard_ = nullptr;
   ui::NoteGrid *grid_ = nullptr;
   ui::Scope *scope_ = nullptr;
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
