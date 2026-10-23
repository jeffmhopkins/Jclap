// Visage knob panel for the MicroMonsta 2 controller. Everything is laid out on a fixed
// kDesignWidth x kDesignHeight grid and scaled to the window, which keeps its aspect ratio.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>

#include <string>
#include <tuple>
#include <vector>

#include "cc_map.h"

namespace mm2 {
namespace {

namespace ui = jclap::ui;
using ui::Rect;

constexpr float kDesignWidth = 1160.0f;
constexpr float kDesignHeight = 732.0f;
constexpr float kHeaderHeight = 92.0f;
// The panels were drawn under a 72 point title bar; this moves them below the real one.
constexpr float kBodyTop = kHeaderHeight - 72.0f;

// Palette taken from the mock-up; the rest comes from the default jclap::ui theme.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      buildLayout();
      for (auto [button, x, w] : {std::tuple{&chDown_, 408.0f, 34.0f}, {&channel_, 448.0f, 150.0f},
                                  {&chUp_, 604.0f, 34.0f}, {&sendAll_, 656.0f, 110.0f}})
         place(*button, {x, 29, w, 34});

      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
      syncChannel();
      startTimer(30);
   }

   void timerCallback() override { syncChannel(); }

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
      canvas.text("Micro", ui::font(30 * s), visage::Font::kLeft, 70 * s, 24 * s, 100 * s, 44 * s);
      canvas.setColor(t.text);
      canvas.text("Monsta 2", ui::font(30 * s), visage::Font::kLeft, 148 * s, 24 * s, 160 * s, 44 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A small waveform glyph like the mock's logo.
      const float pts[][2] = {{22, 47}, {30, 47}, {34, 37}, {39, 58}, {44, 30}, {49, 62}, {53, 41}, {57, 47}, {62, 47}};
      canvas.setColor(kOrange);
      for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); ++i)
         canvas.segment(pts[i][0] * s, pts[i][1] * s, pts[i + 1][0] * s, pts[i + 1][1] * s, 2.5f * s, true);
   }

   void syncChannel() { channel_.setText("MIDI CH " + std::to_string(host_.channel())); }

   void knob(int cc, unsigned accent, Rect cell) {
      const int i = indexOfCc(cc);
      if (i < 0)
         return;
      const CcParam &p = kCcParams[i];
      auto &k = add<ui::Knob>(cell, ui::ValueRange{0.0, 127.0, defaultValue(p), 1.0, p.bipolar});
      k.setLabel(p.label);
      k.setCaption("CC " + std::to_string(p.cc));
      k.setAccent(accent);
      k.valueText = [](double v) { return std::to_string(toCcValue(v)); };
      bindings_.bind(k, static_cast<clap_id>(p.cc));
   }

   // Knobs for the given CCs, spread evenly across a row.
   void knobs(std::initializer_list<int> ccs, unsigned accent, float x, float y, float w, float h = 100) {
      const std::vector<Rect> cells = ui::row({x, y, w, h}, static_cast<int>(ccs.size()));
      size_t c = 0;
      for (int cc : ccs)
         knob(cc, accent, cells[c++]);
   }

   void box(const char *title, float x, float y, float w, float h) { add<ui::Section>({x, y, w, h}, title); }
   void heading(const char *text, unsigned color, float x, float y, float w) {
      add<ui::Label>({x, y, w, 18}, text, color);
   }
   void divider(float x, float y, float h) { add<ui::Divider>({x, y, 1, h}); }

   void buildLayout() {
      // Sections and labels first so the knobs sit on top of them.
      const float y0 = kBodyTop;
      box("OSCILLATORS", 12, y0 + 84, 780, 160);
      box("MIX", 12, y0 + 256, 340, 136);
      box("FILTER", 364, y0 + 256, 428, 136);
      box("ENVELOPES", 804, y0 + 84, 344, 308);
      box("MOD MATRIX", 12, y0 + 404, 780, 136);
      box("LFO", 804, y0 + 404, 344, 136);
      box("EFFECTS", 12, y0 + 552, 628, 148);
      box("VOICE", 652, y0 + 552, 496, 148);

      // Header knobs.
      knobs({1, 70, 7}, kOrange, 880, 2, 270, 88);

      // Oscillators: three groups of shape / tune / fine tune.
      const unsigned oscColors[] = {kOrange, kBlue, kPurple};
      const char *oscNames[] = {"OSC1", "OSC2", "OSC3"};
      for (int o = 0; o < 3; ++o) {
         const float gx = 22 + o * 256.7f;
         heading(oscNames[o], oscColors[o], gx, y0 + 114, 250);
         if (o > 0)
            divider(gx - 4, y0 + 116, 116);
         knobs({39 + 3 * o, 40 + 3 * o, 41 + 3 * o}, oscColors[o], gx, y0 + 136, 250);
      }

      knobs({48, 49, 50, 51}, kOrange, 22, y0 + 286, 320);
      knobs({74, 71, 88, 89, 90}, kOrange, 374, y0 + 286, 408);

      // Envelopes: one row each, with the row name on the left.
      const unsigned envColors[] = {kOrange, kBlue, kPurple};
      const char *envNames[] = {"ENV1", "ENV2", "ENV3"};
      const int envCcs[3][4] = {{73, 75, 79, 72}, {80, 81, 82, 83}, {84, 85, 86, 87}};
      for (int e = 0; e < 3; ++e) {
         const float y = y0 + 112 + e * 92.0f;
         heading(envNames[e], envColors[e], 806, y + 36, 56);
         knobs({envCcs[e][0], envCcs[e][1], envCcs[e][2], envCcs[e][3]}, envColors[e], 862, y, 280, 92);
      }

      knobs({52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62}, kOrange, 18, y0 + 434, 768);
      knobs({76, 77, 78}, kOrange, 814, y0 + 434, 324);

      heading("DELAY", kBlue, 22, y0 + 576, 258);
      heading("REVERB", kPurple, 290, y0 + 576, 258);
      heading("EQ", kGreen, 552, y0 + 576, 82);
      divider(285, y0 + 580, 108);
      divider(550, y0 + 580, 108);
      knobs({26, 27, 28}, kBlue, 22, y0 + 596, 258);
      knobs({29, 30, 31}, kPurple, 290, y0 + 596, 258);
      knob(25, kGreen, {552, y0 + 596, 82, 100});

      knobs({5, 33, 34, 91, 35, 36, 37}, kOrange, 658, y0 + 590, 484);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
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

} // namespace mm2
