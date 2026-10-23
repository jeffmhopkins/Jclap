// The panel, on a 600 x 320 grid. It uses one of every jclap::ui widget so there is always an
// example to copy: knob, horizontal and vertical slider, toggle and push button, dropdown, XY pad,
// meter, section, label and divider.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>
#include <jclap/ui/xy_pad.h>

#include "params.h"

namespace plugin_template {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 600.0f;
constexpr float kDesignHeight = 320.0f;

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

ui::ValueRange rangeOf(clap_id id, double step = 0.0, bool bipolar = false) {
   return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, bipolar};
}

// Value text and typed-in values for a knob or slider, both from params.h.
void useParamText(ui::ValueControl &control, clap_id id) {
   control.valueText = [id](double v) { return text(id, v); };
   control.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::Section>({10, 10, 580, 300}, "PLUGIN TEMPLATE");

      // Left column: knob, dropdown, toggle and push button.
      auto &gain = add<ui::Knob>({24, 40, 124, 124}, rangeOf(kGain, 0.5));
      gain.setLabel("GAIN");
      useParamText(gain, kGain);
      bindings_.bind(gain, kGain);

      add<ui::Label>({28, 174, 116, 16}, "ROUTING", ui::defaultTheme().dimText, visage::Font::kLeft);
      std::vector<std::string> routings(kRoutingNames, kRoutingNames + kRoutingCount);
      bindings_.bind(add<ui::Dropdown>({28, 192, 116, 30}, routings), kRouting);

      bindings_.bind(add<ui::Button>({28, 238, 56, 30}, "MUTE"), kMute);
      // A plain push button: not bound to a param, it runs onClick.
      add<ui::Button>({88, 238, 56, 30}, "RESET").onClick = [this] { resetAll(); };

      add<ui::Divider>({162, 44, 1, 252});

      // Middle: a horizontal slider, and an XY pad driving pan and width together.
      auto &pan = add<ui::Slider>({178, 40, 220, 44}, ui::Slider::Orientation::Horizontal, rangeOf(kPan, 0.01, true));
      pan.setLabel("PAN");
      useParamText(pan, kPan);
      bindings_.bind(pan, kPan);

      auto &pad = add<ui::XYPad>({198, 96, 180, 200}, rangeOf(kPan, 0.01, true), rangeOf(kWidth, 0.01));
      pad.x().label = "PAN";
      pad.y().label = "WIDTH";
      pad.x().valueText = [](double v) { return text(kPan, v); };
      pad.y().valueText = [](double v) { return text(kWidth, v); };
      bindings_.bind(pad, kPan, kWidth);

      add<ui::Divider>({414, 44, 1, 252});

      // Right: a vertical slider and the output meter.
      auto &width = add<ui::Slider>({432, 40, 64, 256}, ui::Slider::Orientation::Vertical, rangeOf(kWidth, 0.01));
      width.setLabel("WIDTH");
      useParamText(width, kWidth);
      bindings_.bind(width, kWidth);

      add<ui::Label>({516, 40, 56, 16}, "OUT", ui::defaultTheme().dimText);
      meter_ = &add<ui::Meter>({534, 62, 20, 234});
      meter_->setLevel(host_.outputPeak());
      startTimer(30);
   }

   void timerCallback() override { meter_->setLevel(host_.outputPeak()); }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }

private:
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
   ui::Meter *meter_ = nullptr;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath) {
   Panel panel(host);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace plugin_template
