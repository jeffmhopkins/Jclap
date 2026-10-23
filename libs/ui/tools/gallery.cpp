// Renders every jclap::ui widget to a PNG without a host: jclap-ui-gallery out.png

#include <jclap/ui/button.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/window.h>

#include <cstdio>

namespace ui = jclap::ui;

namespace {

class Gallery : public visage::Frame {
public:
   Gallery() {
      for (visage::Frame *f : std::initializer_list<visage::Frame *>{&section_, &heading_, &divider_, &knob_, &bipolar_,
                                                                      &hSlider_, &vSlider_, &button_, &toggle_})
         addChild(f);
      knob_.setLabel("CUTOFF");
      knob_.setCaption("CC 74");
      knob_.setValue(0.62, 0.8);
      bipolar_.setLabel("PAN");
      bipolar_.setAccent(0xff9fc3ee);
      bipolar_.setValue(0.3);
      hSlider_.setLabel("Mix");
      hSlider_.setValue(0.4, 0.55);
      vSlider_.setLabel("LEVEL");
      vSlider_.setAccent(0xffbb8fec);
      vSlider_.setValue(0.7);
      toggle_.setToggle(true);
      toggle_.setOn(true);
   }

   void resized() override {
      section_.setBounds(10, 10, 580, 240);
      heading_.setBounds(30, 40, 200, 18);
      divider_.setBounds(250, 50, 1, 180);
      knob_.setBounds(30, 70, 100, 100);
      bipolar_.setBounds(130, 70, 100, 100);
      hSlider_.setBounds(270, 50, 200, 40);
      vSlider_.setBounds(500, 40, 60, 200);
      button_.setBounds(270, 120, 100, 34);
      toggle_.setBounds(270, 170, 100, 34);
   }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }

private:
   ui::Section section_{"GALLERY"};
   ui::Label heading_{"KNOBS", 0xfff28c28};
   ui::Divider divider_;
   ui::Knob knob_{ui::ValueRange{0.0, 1.0, 0.5}};
   ui::Knob bipolar_{ui::ValueRange{0.0, 1.0, 0.5, 0.0, true}};
   ui::Slider hSlider_{ui::Slider::Orientation::Horizontal};
   ui::Slider vSlider_{ui::Slider::Orientation::Vertical};
   ui::Button button_{"BUTTON"};
   ui::Button toggle_{"TOGGLE"};
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png\n", argv[0]);
      return 1;
   }
   Gallery gallery;
   return ui::renderToPng(gallery, 600, 260, argv[1]) ? 0 : 1;
}
