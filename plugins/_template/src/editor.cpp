// The panel, on a 600 x 360 grid with three tabs. Between them it uses one of every jclap::ui widget,
// so there is always an example to copy:
//
//    MIXER:      knob, horizontal and vertical slider, dropdown, toggle and push button, XY pad,
//                meter, label, divider
//    TEST TONE:  envelope editor, segmented switch, stepper and number box, activity light,
//                scope, piano keyboard
//    GRID:       note grid with MPE display
//    everywhere: section, tab bar, tooltips

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/keyboard.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/scope.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/xy_pad.h>

#include <utility>
#include <vector>

#include "params.h"

namespace plugin_template {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 600.0f;
constexpr float kDesignHeight = 360.0f;
constexpr int kScopeSamples = 1024;

enum Page { kMixerPage, kTonePage, kGridPage };

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0, bool bipolar = false) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, bipolar};
}

std::vector<std::string> namesOf(clap_id id) {
   int count = 0;
   const char *const *names = choiceNames(id, count);
   return std::vector<std::string>(names, names + count);
}

// Value text and typed-in values for a knob, slider or number box, both from params.h.
void useParamText(ui::ValueControl &control, clap_id id) {
   control.valueText = [id](double v) { return text(id, v); };
   control.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::Section>({10, 10, 580, 340}, "PLUGIN TEMPLATE");
      tabs_ = &add<ui::TabBar>({270, 14, 310, 26}, std::vector<std::string>{"MIXER", "TEST TONE", "GRID"});
      buildMixer();
      buildTestTone();
      buildGrid();
      startTimer(30);
   }

   void showPage(int page) { tabs_->select(page); }

   void timerCallback() override {
      meter_->setLevel(host_.outputPeak());
      led_->setCount(host_.noteCount());
      for (int n = keyboard_->lowNote(); n <= keyboard_->highNote(); ++n)
         keyboard_->setNoteOn(n, host_.noteHeld(n));
      EditorHost::Voice voice;
      if (host_.voice(voice))
         grid_->setTouches({{voice.note, voice.bend, voice.pressure, voice.timbre}});
      else
         grid_->setTouches({});
      if (scope_->isVisible()) {
         host_.readScope(scopeSamples_.data(), scopeSamples_.size());
         scope_->setSamples(scopeSamples_);
      }
   }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }

private:
   template <typename T, typename... Args>
   T &onPage(Page page, ui::Rect bounds, Args &&...args) {
      return tabs_->addToPage(page, add<T>(bounds, std::forward<Args>(args)...));
   }

   void buildMixer() {
      const unsigned dim = ui::defaultTheme().dimText;

      // Left column: knob, dropdown, toggle and push button.
      auto &gain = onPage<ui::Knob>(kMixerPage, {24, 56, 124, 124}, rangeOf(kGain, 0.5));
      gain.setLabel("GAIN");
      useParamText(gain, kGain);
      bindings_.bind(gain, kGain);
      tips_.add(gain, "Output level. Drag, or right-click to type a value.");

      onPage<ui::Label>(kMixerPage, {28, 194, 116, 16}, "ROUTING", dim, visage::Font::kLeft);
      bindings_.bind(onPage<ui::Dropdown>(kMixerPage, {28, 212, 116, 30}, namesOf(kRouting)), kRouting);

      bindings_.bind(onPage<ui::Button>(kMixerPage, {28, 270, 56, 30}, "MUTE"), kMute);
      // A plain push button: not bound to a param, it runs onClick.
      auto &reset = onPage<ui::Button>(kMixerPage, {88, 270, 56, 30}, "RESET");
      reset.onClick = [this] { resetAll(); };
      tips_.add(reset, "Set every parameter back to its default.");

      onPage<ui::Divider>(kMixerPage, {162, 60, 1, 276});

      // Middle: a horizontal slider, and an XY pad driving pan and width together.
      auto &pan = onPage<ui::Slider>(kMixerPage, {178, 56, 220, 44}, ui::Slider::Orientation::Horizontal,
                                     rangeOf(kPan, 0.01, true));
      pan.setLabel("PAN");
      useParamText(pan, kPan);
      bindings_.bind(pan, kPan);

      auto &pad = onPage<ui::XYPad>(kMixerPage, {198, 112, 180, 220}, rangeOf(kPan, 0.01, true), rangeOf(kWidth, 0.01));
      pad.x().label = "PAN";
      pad.y().label = "WIDTH";
      pad.x().valueText = [](double v) { return text(kPan, v); };
      pad.y().valueText = [](double v) { return text(kWidth, v); };
      bindings_.bind(pad, kPan, kWidth);

      onPage<ui::Divider>(kMixerPage, {414, 60, 1, 276});

      // Right: a vertical slider and the output meter.
      auto &width = onPage<ui::Slider>(kMixerPage, {432, 56, 64, 280}, ui::Slider::Orientation::Vertical,
                                       rangeOf(kWidth, 0.01));
      width.setLabel("WIDTH");
      useParamText(width, kWidth);
      bindings_.bind(width, kWidth);

      onPage<ui::Label>(kMixerPage, {516, 56, 56, 16}, "OUT", dim);
      meter_ = &onPage<ui::Meter>(kMixerPage, {534, 78, 20, 258});
   }

   void buildTestTone() {
      const unsigned dim = ui::defaultTheme().dimText;

      // The envelope: four params edited on one drawing.
      auto &env = onPage<ui::EnvelopeEditor>(kTonePage, {24, 52, 262, 170}, rangeOf(kAttack, 1.0), rangeOf(kDecay, 1.0),
                                             rangeOf(kSustain, 0.01), rangeOf(kRelease, 1.0));
      for (clap_id id : {kAttack, kDecay, kSustain, kRelease})
         env.stage(static_cast<ui::EnvelopeEditor::Stage>(id - kAttack)).valueText = [id](double v) { return text(id, v); };
      bindings_.bind(env, kAttack, kDecay, kSustain, kRelease);
      tips_.add(env, "Drag the corners: attack, decay and sustain, release.");

      onPage<ui::Label>(kTonePage, {302, 50, 120, 16}, "WAVE", dim, visage::Font::kLeft);
      bindings_.bind(onPage<ui::SegmentedSwitch>(kTonePage, {302, 68, 274, 28}, namesOf(kWave)), kWave);

      // A stepper (number box with - and +) and a plain number box.
      onPage<ui::Label>(kTonePage, {302, 106, 132, 16}, "TRANSPOSE", dim, visage::Font::kLeft);
      auto &transpose = onPage<ui::NumberBox>(kTonePage, {302, 124, 132, 28}, rangeOf(kTranspose, 1.0));
      transpose.setStepButtons(true);
      useParamText(transpose, kTranspose);
      bindings_.bind(transpose, kTranspose);

      onPage<ui::Label>(kTonePage, {444, 106, 132, 16}, "LEVEL", dim, visage::Font::kLeft);
      auto &level = onPage<ui::NumberBox>(kTonePage, {444, 124, 132, 28}, rangeOf(kToneLevel, 0.5));
      useParamText(level, kToneLevel);
      bindings_.bind(level, kToneLevel);
      tips_.add(level, "Drag up or down, scroll, or double-click to reset.");

      led_ = &onPage<ui::ActivityLed>(kTonePage, {302, 164, 120, 16}, "NOTE IN");
      scope_ = &onPage<ui::Scope>(kTonePage, {302, 186, 274, 36});

      // The keyboard plays the tone, and lights the notes the host sends.
      keyboard_ = &onPage<ui::PianoKeyboard>(kTonePage, {24, 236, 552, 100}, 48, 84);
      keyboard_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      keyboard_->onNoteOff = [this](int note) { host_.noteOff(note); };
   }

   void buildGrid() {
      // Plays the test tone too: press a cell, slide sideways to bend, up and down for timbre.
      // Notes from an MPE controller show their bend, pressure and timbre on it.
      grid_ = &onPage<ui::NoteGrid>(kGridPage, {24, 52, 552, 284}, 16, 8, 36);
      grid_->onNoteOn = [this](int note, float velocity) { host_.noteOn(note, velocity); };
      grid_->onExpression = [this](int note, float bend, float pressure, float timbre) {
         host_.noteExpression(note, bend, pressure, timbre);
      };
      grid_->onNoteOff = [this](int note) { host_.noteOff(note); };
   }

   // Sets every param back to its default, as one gesture each so the host records it.
   void resetAll() {
      for (clap_id id = 0; id < kParamCount; ++id) {
         host_.beginEdit(id);
         host_.setParam(id, kParams[id].defaultValue);
         host_.endEdit(id);
      }
      bindings_.sync();
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::TabBar *tabs_ = nullptr;
   ui::Meter *meter_ = nullptr;
   ui::ActivityLed *led_ = nullptr;
   ui::Scope *scope_ = nullptr;
   ui::PianoKeyboard *keyboard_ = nullptr;
   ui::NoteGrid *grid_ = nullptr;
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
   // Twice, so a note count that changed in between shows the activity light lit.
   panel.timerCallback();
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace plugin_template
