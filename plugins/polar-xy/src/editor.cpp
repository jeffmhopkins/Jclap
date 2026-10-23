// Polar XY's panel, in the MicroMonsta 2 style: a title bar, live views of the XY input and the polar
// output, and the settings, all on a 720 x 480 design grid scaled to the window.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/xy_pad.h>

#include <string>

#include "params.h"
#include "polar.h"

namespace polar_xy {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 480.0f;
constexpr float kHeaderHeight = 64.0f;

// The MicroMonsta 2 palette.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      add<ui::Section>({12, 76, 232, 260}, "XY IN");
      add<ui::Section>({256, 76, 232, 260}, "POLAR OUT");
      add<ui::Section>({500, 76, 208, 124}, "INPUT CC");
      add<ui::Section>({500, 212, 208, 124}, "OUTPUT CC");
      add<ui::Section>({12, 348, 696, 120}, "MAPPING");

      // Live views: what comes in on the X/Y CCs, and the angle/radius that goes out. Display only.
      const ui::ValueRange cc{0.0, 127.0, 64.0, 1.0};
      xy_ = &add<ui::XYPad>({32, 106, 192, 216}, cc, cc);
      xy_->setAccent(kBlue);
      polar_ = &add<ui::XYPad>({276, 106, 192, 216}, ui::ValueRange{0.0, 127.0, 0.0, 1.0},
                               ui::ValueRange{0.0, 127.0, 0.0, 1.0});
      polar_->setGrid(ui::XYPad::Grid::Polar);
      polar_->setAccent(kOrange);
      for (ui::XYPad *pad : {xy_, polar_})
         pad->setIgnoresMouseEvents(true, false);

      const auto in = ui::row({508, 104, 192, 92}, 2);
      knob(kParamInX, "X", kBlue, in[0]);
      knob(kParamInY, "Y", kBlue, in[1]);
      const auto out = ui::row({508, 240, 192, 92}, 2);
      knob(kParamOutAngle, "ANGLE", kOrange, out[0]);
      knob(kParamOutRadius, "RADIUS", kOrange, out[1]);

      auto &offset = add<ui::Knob>({36, 374, 96, 90}, ui::ValueRange{0.0, 360.0, 0.0, 1.0});
      offset.setLabel("OFFSET");
      offset.setAccent(kPurple);
      offset.valueText = [](double v) { return text(kParamAngleOffset, v); };
      offset.parseText = [](const std::string &t, double &v) { return parseParam(kParamAngleOffset, t.c_str(), v); };
      bindings_.bind(offset, kParamAngleOffset);

      choice(kParamDirection, "DIRECTION", kDirectionLabels, 150);
      choice(kParamRadiusMode, "RADIUS MODE", kRadiusModeLabels, 370);
      add<ui::Label>({590, 392, 106, 16}, "PASS INPUT CCS", kPurple, visage::Font::kLeft);
      auto &pass = add<ui::Button>({590, 410, 106, 30}, "PASS");
      pass.setAccent(kPurple);
      bindings_.bind(pass, kParamPassThrough);

      sync();
      startTimer(30);
   }

   void timerCallback() override { sync(); }

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
      canvas.text("Polar", ui::font(28 * s), visage::Font::kLeft, 58 * s, 12 * s, 90 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("XY", ui::font(28 * s), visage::Font::kLeft, 128 * s, 12 * s, 60 * s, 40 * s);
      canvas.setColor(t.dimText);
      canvas.text("XY CCs to Orb angle and radius", ui::font(13 * s), visage::Font::kRight, 380 * s, 12 * s,
                  320 * s, 40 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small orb: a ring with a radius line out to a dot.
      canvas.setColor(kOrange);
      canvas.ring(18 * s, 16 * s, 32 * s, 2.0f * s);
      canvas.segment(34 * s, 32 * s, 44 * s, 23 * s, 2.0f * s, true);
      canvas.circle(41 * s, 19 * s, 7 * s);
   }

   void knob(clap_id id, const char *label, unsigned accent, ui::Rect cell) {
      const ParamInfo &p = kParams[id];
      auto &k = add<ui::Knob>(cell, ui::ValueRange{p.min, p.max, p.def, 1.0});
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
   }

   void choice(clap_id id, const char *label, const char *const *labels, float x) {
      add<ui::Label>({x, 392, 200, 16}, label, kPurple, visage::Font::kLeft);
      auto &d = add<ui::Dropdown>({x, 410, 200, 30}, std::vector<std::string>{labels[0], labels[1]});
      d.setAccent(kPurple);
      bindings_.bind(d, id);
   }

   void sync() {
      int x = 64, y = 64;
      host_.lastInput(x, y);
      jclap::PolarSettings settings;
      settings.angleOffsetDeg = host_.paramValue(kParamAngleOffset);
      settings.clockwise = host_.paramValue(kParamDirection) >= 0.5;
      settings.radiusMode = host_.paramValue(kParamRadiusMode) >= 0.5 ? jclap::RadiusMode::FillSquare
                                                                       : jclap::RadiusMode::ClampToCircle;
      const jclap::PolarCC out = jclap::toPolar(x, y, settings);
      // At dead center the angle is undefined and the plugin holds the last one; so does the view.
      if (out.hasAngle)
         angle_ = out.angle;

      xy_->x().label = "X " + std::to_string(x);
      xy_->y().label = "Y " + std::to_string(y);
      polar_->x().label = "ANGLE " + std::to_string(angle_);
      polar_->y().label = "RADIUS " + std::to_string(out.radius);
      xy_->setValues(x, y);
      polar_->setValues(angle_, out.radius);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::XYPad *xy_ = nullptr;
   ui::XYPad *polar_ = nullptr;
   int angle_ = 0;
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

} // namespace polar_xy
