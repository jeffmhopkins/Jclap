// Visage knob panel for the MicroMonsta 2 controller. Everything is laid out on a fixed
// kDesignWidth x kDesignHeight grid and scaled to the window, which keeps its aspect ratio.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/section.h>

#include <memory>
#include <string>
#include <vector>

#include "cc_map.h"

namespace mm2 {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 1160.0f;
constexpr float kDesignHeight = 732.0f;
constexpr float kHeaderHeight = 92.0f;

// Palette taken from the mock-up; the rest comes from the default jclap::ui theme.
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;

// A frame and where it goes on the design grid.
template <typename T>
struct Placed {
   std::unique_ptr<T> frame;
   float x, y, w, h;
};

class Panel : public visage::Frame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : host_(host) {
      buildLayout();
      // Decorations first so the controls sit on top of them.
      for (auto &s : sections_)
         addChild(s.frame.get());
      for (auto &l : labels_)
         addChild(l.frame.get());
      for (auto &d : dividers_)
         addChild(d.frame.get());
      for (auto &k : knobs_)
         addChild(k.frame.get());
      for (ui::Button *b : {&chDown_, &chUp_, &channel_, &sendAll_})
         addChild(b);

      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
      syncChannel();
      startTimer(30);
   }

   // Picks up changes made by the host (automation, modulation, state load).
   void timerCallback() override {
      for (size_t i = 0; i < knobs_.size(); ++i)
         knobs_[i].frame->setValue(host_.baseValue(knobIndex_[i]), host_.modulatedValue(knobIndex_[i]));
      syncChannel();
   }

   void resized() override {
      const float s = width() / kDesignWidth;
      const auto layout = [s](auto &list) {
         for (auto &p : list)
            p.frame->setBounds(p.x * s, p.y * s, p.w * s, p.h * s);
      };
      layout(sections_);
      layout(labels_);
      layout(dividers_);
      layout(knobs_);
      for (auto &sec : sections_)
         sec.frame->setScale(s);
      chDown_.setBounds(408 * s, 29 * s, 34 * s, 34 * s);
      channel_.setBounds(448 * s, 29 * s, 150 * s, 34 * s);
      chUp_.setBounds(604 * s, 29 * s, 34 * s, 34 * s);
      sendAll_.setBounds(656 * s, 29 * s, 110 * s, 34 * s);
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = width() / kDesignWidth;
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

   void place(int cc, unsigned accent, float x, float y, float w, float h = 100) {
      const int i = indexOfCc(cc);
      if (i < 0)
         return;
      const CcParam &p = kCcParams[i];
      auto knob = std::make_unique<ui::Knob>(ui::ValueRange{0.0, 127.0, defaultValue(p), 1.0, p.bipolar});
      knob->setLabel(p.label);
      knob->setCaption("CC " + std::to_string(p.cc));
      knob->setAccent(accent);
      knob->valueText = [](double v) { return std::to_string(toCcValue(v)); };
      knob->onBeginEdit = [this, i] { host_.beginEdit(i); };
      knob->onChange = [this, i](double v) { host_.setValue(i, v); };
      knob->onEndEdit = [this, i] { host_.endEdit(i); };
      knob->modulatedSource = [this, i] { return host_.modulatedValue(i); };
      knob->setValue(host_.baseValue(i), host_.modulatedValue(i));
      knobs_.push_back({std::move(knob), x, y, w, h});
      knobIndex_.push_back(i);
   }

   // Places knobs for consecutive CCs evenly across a row.
   void row(std::initializer_list<int> ccs, unsigned accent, float x, float y, float w, float h = 100) {
      const float cell = w / static_cast<float>(ccs.size());
      float cx = x;
      for (int cc : ccs) {
         place(cc, accent, cx, y, cell, h);
         cx += cell;
      }
   }

   void box(const char *title, float x, float y, float w, float h) {
      sections_.push_back({std::make_unique<ui::Section>(title), x, y, w, h});
   }

   void heading(const char *text, unsigned color, float x, float y, float w) {
      labels_.push_back({std::make_unique<ui::Label>(text, color), x, y, w, 18});
   }

   void divider(float x, float y, float h) { dividers_.push_back({std::make_unique<ui::Divider>(), x, y, 1, h}); }

   void buildLayout() {
      // Header knobs.
      row({1, 70, 7}, kOrange, 880, 2, 270, 88);

      // Oscillators: three groups of shape / tune / fine tune.
      box("OSCILLATORS", 12, 84, 780, 160);
      const unsigned oscColors[] = {kOrange, kBlue, kPurple};
      const char *oscNames[] = {"OSC1", "OSC2", "OSC3"};
      for (int o = 0; o < 3; ++o) {
         const float gx = 22 + o * 256.7f;
         heading(oscNames[o], oscColors[o], gx, 114, 250);
         row({39 + 3 * o, 40 + 3 * o, 41 + 3 * o}, oscColors[o], gx, 136, 250);
         if (o > 0)
            divider(gx - 4, 116, 116);
      }

      box("MIX", 12, 256, 340, 136);
      row({48, 49, 50, 51}, kOrange, 22, 286, 320);

      box("FILTER", 364, 256, 428, 136);
      row({74, 71, 88, 89, 90}, kOrange, 374, 286, 408);

      // Envelopes: one row each, with the row name on the left.
      box("ENVELOPES", 804, 84, 344, 308);
      const unsigned envColors[] = {kOrange, kBlue, kPurple};
      const char *envNames[] = {"ENV1", "ENV2", "ENV3"};
      const int envCcs[3][4] = {{73, 75, 79, 72}, {80, 81, 82, 83}, {84, 85, 86, 87}};
      for (int e = 0; e < 3; ++e) {
         const float y = 112 + e * 92.0f;
         heading(envNames[e], envColors[e], 806, y + 36, 56);
         row({envCcs[e][0], envCcs[e][1], envCcs[e][2], envCcs[e][3]}, envColors[e], 862, y, 280, 92);
      }

      box("MOD MATRIX", 12, 404, 780, 136);
      row({52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62}, kOrange, 18, 434, 768);

      box("LFO", 804, 404, 344, 136);
      row({76, 77, 78}, kOrange, 814, 434, 324);

      box("EFFECTS", 12, 552, 628, 148);
      heading("DELAY", kBlue, 22, 576, 258);
      heading("REVERB", kPurple, 290, 576, 258);
      heading("EQ", kGreen, 552, 576, 82);
      row({26, 27, 28}, kBlue, 22, 596, 258);
      row({29, 30, 31}, kPurple, 290, 596, 258);
      place(25, kGreen, 552, 596, 82);
      divider(285, 580, 108);
      divider(550, 580, 108);

      box("VOICE", 652, 552, 496, 148);
      row({5, 33, 34, 91, 35, 36, 37}, kOrange, 658, 590, 484);

      // The panels above were laid out under a 72 point title bar; move them below the real one.
      const float shift = kHeaderHeight - 72.0f;
      for (auto &k : knobs_)
         if (k.y > 50)
            k.y += shift;
      for (auto &sec : sections_)
         sec.y += shift;
      for (auto &l : labels_)
         l.y += shift;
      for (auto &d : dividers_)
         d.y += shift;
   }

   EditorHost &host_;
   std::vector<Placed<ui::Knob>> knobs_;
   std::vector<int> knobIndex_; // kCcParams index of each knob
   std::vector<Placed<ui::Section>> sections_;
   std::vector<Placed<ui::Label>> labels_;
   std::vector<Placed<ui::Divider>> dividers_;
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
