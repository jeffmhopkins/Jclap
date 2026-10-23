// The panel: a gain knob, a pan slider, a mute toggle and an output meter on a 360 x 240 grid.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/slider.h>

#include "params.h"

namespace plugin_template {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 360.0f;
constexpr float kDesignHeight = 240.0f;

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
      add<ui::Section>({10, 10, 340, 220}, "PLUGIN TEMPLATE");

      auto &gain = add<ui::Knob>({20, 50, 110, 110}, rangeOf(kGain, 0.5));
      gain.setLabel("GAIN");
      gain.valueText = [](double v) { return text(kGain, v); };
      gain.parseText = [](const std::string &t, double &v) { return parseParam(kGain, t.c_str(), v); };
      bindings_.bind(gain, kGain);

      auto &pan = add<ui::Slider>({150, 60, 140, 44}, ui::Slider::Orientation::Horizontal, rangeOf(kPan, 0.01, true));
      pan.setLabel("PAN");
      pan.valueText = [](double v) { return text(kPan, v); };
      pan.parseText = [](const std::string &t, double &v) { return parseParam(kPan, t.c_str(), v); };
      bindings_.bind(pan, kPan);

      bindings_.bind(add<ui::Button>({150, 130, 140, 32}, "MUTE"), kMute);

      meter_ = &add<ui::Meter>({310, 50, 16, 160});
      meter_->setLevel(host_.outputPeak());
      startTimer(30);
   }

   void timerCallback() override { meter_->setLevel(host_.outputPeak()); }

   void draw(visage::Canvas &canvas) override {
      canvas.setColor(ui::defaultTheme().background);
      canvas.fill(0, 0, width(), height());
   }

private:
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
