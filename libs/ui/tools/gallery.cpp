// Renders every jclap::ui widget to a PNG without a host: jclap-ui-gallery out.png

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/keyboard.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/scope.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/window.h>
#include <jclap/ui/xy_pad.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace ui = jclap::ui;

namespace {

class Gallery : public ui::DesignFrame {
public:
   Gallery() : DesignFrame(760, 540) {
      add<ui::Section>({10, 10, 740, 240}, "GALLERY");
      add<ui::Label>({30, 40, 200, 18}, "KNOBS", 0xfff28c28);
      add<ui::Divider>({250, 50, 1, 180});
      add<ui::Divider>({490, 50, 1, 180});

      auto &knob = add<ui::Knob>({30, 70, 100, 100}, ui::ValueRange{0.0, 1.0, 0.5});
      knob.setLabel("CUTOFF");
      knob.setCaption("CC 74");
      knob.setValue(0.62, 0.8);
      auto &pan = add<ui::Knob>({130, 70, 100, 100}, ui::ValueRange{0.0, 1.0, 0.5, 0.0, true});
      pan.setLabel("PAN");
      pan.setAccent(0xff9fc3ee);
      pan.setValue(0.3);

      auto &mix = add<ui::Slider>({270, 50, 200, 40});
      mix.setLabel("Mix");
      mix.setValue(0.4, 0.55);
      add<ui::Button>({270, 110, 95, 30}, "BUTTON");
      auto &toggle = add<ui::Button>({375, 110, 95, 30}, "TOGGLE");
      toggle.setToggle(true);
      toggle.setOn(true);
      add<ui::Dropdown>({270, 160, 200, 30}, std::vector<std::string>{"Sine", "Saw", "Square"}).setSelected(1);

      auto &level = add<ui::Slider>({505, 40, 50, 200}, ui::Slider::Orientation::Vertical);
      level.setLabel("LEVEL");
      level.setAccent(0xffbb8fec);
      level.setValue(0.7);
      add<ui::Meter>({565, 50, 12, 180}).setLevelDb(-8.0f);

      auto &pad = add<ui::XYPad>({595, 40, 140, 160});
      pad.x().label = "ANGLE";
      pad.y().label = "RADIUS";
      pad.setValues(0.3, 0.45, 0.7, 0.6);

      add<ui::Section>({10, 260, 740, 270}, "MORE");
      add<ui::TabBar>({30, 290, 220, 26}, std::vector<std::string>{"MAIN", "ENV", "FX"});
      add<ui::SegmentedSwitch>({30, 330, 220, 28}, std::vector<std::string>{"Sine", "Saw", "Square"}).setSelected(1);
      auto &stepper = add<ui::NumberBox>({30, 370, 105, 28}, ui::ValueRange{1, 16, 1, 1});
      stepper.setStepButtons(true);
      stepper.setValue(10);
      auto &number = add<ui::NumberBox>({145, 370, 105, 28}, ui::ValueRange{0, 127, 64, 1});
      number.setLabel("CC");
      number.setValue(74);
      auto &led = add<ui::ActivityLed>({30, 410, 100, 16}, "MIDI IN");
      led.setLevel(1.0f);
      tips_.add(led, "Flashes on MIDI input");
      led_ = &led;

      auto &env = add<ui::EnvelopeEditor>({270, 290, 220, 150}, ui::ValueRange{1, 2000, 10}, ui::ValueRange{1, 2000, 300},
                                          ui::ValueRange{0, 1, 0.6}, ui::ValueRange{1, 4000, 500});
      env.setValues(120, 500, 0.55, 1200);

      auto &scope = add<ui::Scope>({505, 290, 120, 150});
      std::vector<float> wave(400);
      for (size_t i = 0; i < wave.size(); ++i)
         wave[i] = 0.8f * std::sin(i * 0.05f) * (1.0f - i / 500.0f);
      scope.setSamples(wave);
      scope.setAccent(0xff9bd36a);

      auto &polar = add<ui::XYPad>({635, 290, 100, 150}, ui::ValueRange{0, 127, 0, 1}, ui::ValueRange{0, 127, 0, 1});
      polar.setGrid(ui::XYPad::Grid::Polar);
      polar.x().label = "ANG";
      polar.y().label = "RAD";
      polar.setValues(20, 100);

      auto &keys = add<ui::PianoKeyboard>({30, 450, 705, 66}, 36, 84);
      for (int note : {48, 55, 64, 67})
         keys.setNoteOn(note, true);
   }

   void showTips() { tips_.show(*tipTarget()); }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }

private:
   visage::Frame *tipTarget() { return led_; }
   ui::Tooltips tips_;
   visage::Frame *led_ = nullptr;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png\n", argv[0]);
      return 1;
   }
   Gallery gallery;
   gallery.setBounds(0, 0, 760, 540);
   gallery.showTips();
   return ui::renderToPng(gallery, 760, 540, argv[1]) ? 0 : 1;
}
