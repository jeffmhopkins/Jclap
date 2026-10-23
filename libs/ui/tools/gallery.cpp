// Renders every jclap::ui widget to a PNG without a host: jclap-ui-gallery out.png

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/window.h>
#include <jclap/ui/xy_pad.h>

#include <cstdio>

namespace ui = jclap::ui;

namespace {

class Gallery : public ui::DesignFrame {
public:
   Gallery() : DesignFrame(760, 260) {
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
   }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png\n", argv[0]);
      return 1;
   }
   Gallery gallery;
   return ui::renderToPng(gallery, 760, 260, argv[1]) ? 0 : 1;
}
