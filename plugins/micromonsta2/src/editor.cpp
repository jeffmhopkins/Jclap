// Visage knob panel for the MicroMonsta 2 controller. Everything is laid out on a fixed
// kDesignWidth x kDesignHeight grid and scaled to the window, which keeps its aspect ratio.

#include "editor.h"

#include <visage/app.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cc_map.h"
#include "embedded/fonts.h"

namespace mm2 {
namespace {

constexpr float kDesignWidth = 1160.0f;
constexpr float kDesignHeight = 732.0f;
constexpr float kHeaderHeight = 92.0f;
constexpr float kPi = 3.14159265358979f;
constexpr float kSweep = 0.75f * kPi; // half of the 270 degree knob travel

// Palette taken from the mock-up.
constexpr unsigned kBackground = 0xff15181d;
constexpr unsigned kPanel = 0xff1c2027;
constexpr unsigned kPanelBorder = 0xff2c323b;
constexpr unsigned kHeaderBar = 0xff111317;
constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff9bd36a;
constexpr unsigned kText = 0xffe6e8eb;
constexpr unsigned kDimText = 0xff8d939c;
constexpr unsigned kTrack = 0xff2e333b;

visage::Font font(float size) { return visage::Font(size, visage::fonts::Lato_Regular_ttf); }

// One labelled knob: label on top, knob, value, CC number.
class Knob : public visage::Frame {
public:
   Knob(EditorHost &host, int index, unsigned accent) : host_(host), index_(index), accent_(accent) {
      value_ = host_.ccValue(index_);
   }

   const CcParam &param() const { return kCcParams[index_]; }

   // Picks up changes made by the host (automation, modulation, state load).
   void sync() {
      const double v = host_.ccValue(index_);
      if (v != value_) {
         value_ = v;
         redraw();
      }
   }

   void draw(visage::Canvas &canvas) override {
      const float w = width(), h = height();
      const float s = h / 100.0f;
      const float d = std::min(w * 0.7f, 48.0f * s);
      const float cx = w * 0.5f, cy = 18.0f * s + d * 0.5f + 2.0f * s;

      canvas.setColor(kText);
      canvas.text(param().label, font(10.5f * s), visage::Font::kCenter, 0, 0, w, 16.0f * s);

      // Track and value arc. Our angles run clockwise from 12 o'clock; Visage's start at 3 o'clock.
      const float t = static_cast<float>(value_ / 127.0);
      const float angle = -kSweep + 2.0f * kSweep * t;
      const float from = param().bipolar ? 0.0f : -kSweep;
      const float ring = 3.2f * s;
      canvas.setColor(kTrack);
      canvas.arc(cx - d * 0.5f, cy - d * 0.5f, d, ring, -0.5f * kPi, kSweep, true);
      if (std::fabs(angle - from) > 0.01f) {
         canvas.setColor(accent_);
         canvas.arc(cx - d * 0.5f, cy - d * 0.5f, d, ring, 0.5f * (angle + from) - 0.5f * kPi,
                    0.5f * std::fabs(angle - from),
                    true);
      }

      // Knob body: soft shadow, shaded cap and a pointer line.
      const float body = d * 0.72f;
      canvas.setColor(0x66000000);
      canvas.circle(cx - body * 0.5f - 1.0f * s, cy - body * 0.5f + 1.5f * s, body + 2.0f * s);
      canvas.setColor(visage::Brush::radial(visage::Color(0xff4b515a), visage::Color(0xff1d2026),
                                            visage::Point(cx - body * 0.15f, cy - body * 0.2f), body * 0.65f,
                                            body * 0.65f));
      canvas.circle(cx - body * 0.5f, cy - body * 0.5f, body);
      canvas.setColor(0xff101216);
      canvas.ring(cx - body * 0.5f, cy - body * 0.5f, body, 1.0f * s);

      const float px = std::sin(angle), py = -std::cos(angle);
      canvas.setColor(kText);
      canvas.segment(cx + px * body * 0.12f, cy + py * body * 0.12f, cx + px * body * 0.4f, cy + py * body * 0.4f,
                     2.0f * s, true);

      const float textY = cy + d * 0.5f + 3.0f * s;
      canvas.setColor(kText);
      canvas.text(std::to_string(toCcValue(value_)), font(12.0f * s), visage::Font::kCenter, 0, textY, w,
                  15.0f * s);
      canvas.setColor(kDimText);
      canvas.text("CC " + std::to_string(param().cc), font(9.5f * s), visage::Font::kCenter, 0,
                  textY + 14.0f * s, w, 13.0f * s);
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (e.repeatClickCount() == 2) {
         host_.beginEdit(index_);
         set(defaultValue(param()));
         host_.endEdit(index_);
         dragging_ = false;
         return;
      }
      dragging_ = true;
      dragStartY_ = e.position.y;
      dragStartValue_ = value_;
      host_.beginEdit(index_);
   }

   void mouseDrag(const visage::MouseEvent &e) override {
      if (!dragging_)
         return;
      // A 200 point drag covers the full range; hold shift for fine control.
      const float range = (e.isShiftDown() ? 2000.0f : 200.0f) * height() / 100.0f;
      set(dragStartValue_ + (dragStartY_ - e.position.y) * 127.0 / range);
   }

   void mouseUp(const visage::MouseEvent &) override {
      if (dragging_)
         host_.endEdit(index_);
      dragging_ = false;
   }

   bool mouseWheel(const visage::MouseEvent &e) override {
      const float delta = e.precise_wheel_delta_y != 0.0f ? e.precise_wheel_delta_y : e.wheel_delta_y;
      if (delta == 0.0f)
         return false;
      host_.beginEdit(index_);
      set(std::round(value_) + (delta > 0 ? 1.0 : -1.0));
      host_.endEdit(index_);
      return true;
   }

private:
   void set(double v) {
      value_ = std::clamp(v, 0.0, 127.0);
      host_.setValue(index_, value_);
      redraw();
   }

   EditorHost &host_;
   int index_;
   unsigned accent_;
   double value_ = 0.0;
   bool dragging_ = false;
   float dragStartY_ = 0.0f;
   double dragStartValue_ = 0.0;
};

// A rounded box that acts as a button; the click handler is set by the panel.
class Button : public visage::Frame {
public:
   explicit Button(std::string text) : text_(std::move(text)) {}

   void setText(std::string text) {
      if (text != text_) {
         text_ = std::move(text);
         redraw();
      }
   }

   std::function<void()> onClick;
   std::function<void(int)> onWheel;

   void draw(visage::Canvas &canvas) override {
      const float r = height() * 0.18f;
      canvas.setColor(hover_ ? 0xff2a3039 : 0xff20252c);
      canvas.roundedRectangle(0, 0, width(), height(), r);
      canvas.setColor(kPanelBorder);
      canvas.roundedRectangleBorder(0, 0, width(), height(), r, 1.0f);
      canvas.setColor(kText);
      canvas.text(text_, font(height() * 0.42f), visage::Font::kCenter, 0, 0, width(), height());
   }

   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &) override {
      if (onClick)
         onClick();
   }
   bool mouseWheel(const visage::MouseEvent &e) override {
      if (!onWheel || e.wheel_delta_y == 0.0f)
         return false;
      onWheel(e.wheel_delta_y > 0 ? 1 : -1);
      return true;
   }

private:
   std::string text_;
   bool hover_ = false;
};

struct Box {
   const char *title;
   float x, y, w, h;
};

struct SubHeading {
   const char *text;
   unsigned color;
   float x, y, w;
};

class Panel : public visage::Frame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : host_(host) {
      buildLayout();
      for (auto &k : knobs_)
         addChild(k.knob.get());
      for (Button *b : {&chDown_, &chUp_, &channel_, &sendAll_})
         addChild(b);

      chDown_.onClick = [this] { host_.setChannel(host_.channel() - 1); };
      chUp_.onClick = [this] { host_.setChannel(host_.channel() + 1); };
      channel_.onWheel = [this](int d) { host_.setChannel(host_.channel() + d); };
      sendAll_.onClick = [this] { host_.sendAll(); };
      syncChannel();
      startTimer(30);
   }

   void timerCallback() override {
      for (auto &k : knobs_)
         k.knob->sync();
      syncChannel();
   }

   void resized() override {
      const float s = width() / kDesignWidth;
      for (auto &k : knobs_)
         k.knob->setBounds(k.x * s, k.y * s, k.w * s, k.h * s);
      chDown_.setBounds(408 * s, 29 * s, 34 * s, 34 * s);
      channel_.setBounds(448 * s, 29 * s, 150 * s, 34 * s);
      chUp_.setBounds(604 * s, 29 * s, 34 * s, 34 * s);
      sendAll_.setBounds(656 * s, 29 * s, 110 * s, 34 * s);
   }

   void draw(visage::Canvas &canvas) override {
      const float s = width() / kDesignWidth;
      canvas.setColor(kBackground);
      canvas.fill(0, 0, width(), height());

      // Title bar.
      canvas.setColor(kHeaderBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kOrange);
      canvas.text("Micro", font(30 * s), visage::Font::kLeft, 70 * s, 24 * s, 100 * s, 44 * s);
      canvas.setColor(kText);
      canvas.text("Monsta 2", font(30 * s), visage::Font::kLeft, 148 * s, 24 * s, 160 * s, 44 * s);

      for (const Box &b : boxes_) {
         canvas.setColor(kPanel);
         canvas.roundedRectangle(b.x * s, b.y * s, b.w * s, b.h * s, 6 * s);
         canvas.setColor(kPanelBorder);
         canvas.roundedRectangleBorder(b.x * s, b.y * s, b.w * s, b.h * s, 6 * s, 1.0f);
         if (b.title[0]) {
            canvas.setColor(kOrange);
            canvas.text(b.title, font(15 * s), visage::Font::kLeft, (b.x + 14) * s, (b.y + 6) * s, b.w * s,
                        22 * s);
         }
      }
      for (const SubHeading &h : headings_) {
         canvas.setColor(h.color);
         canvas.text(h.text, font(13.5f * s), visage::Font::kCenter, h.x * s, h.y * s, h.w * s, 18 * s);
      }
      canvas.setColor(kPanelBorder);
      for (const Box &d : dividers_)
         canvas.fill(d.x * s, d.y * s, std::max(1.0f, d.w * s), d.h * s);
   }

private:
   struct PlacedKnob {
      std::unique_ptr<Knob> knob;
      float x, y, w, h;
   };

   void drawLogo(visage::Canvas &canvas, float s) {
      // A small waveform glyph like the mock's logo.
      const float pts[][2] = {{22, 47}, {30, 47}, {34, 37}, {39, 58}, {44, 30}, {49, 62}, {53, 41}, {57, 47}, {62, 47}};
      canvas.setColor(kOrange);
      for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); ++i)
         canvas.segment(pts[i][0] * s, pts[i][1] * s, pts[i + 1][0] * s, pts[i + 1][1] * s, 2.5f * s, true);
   }

   void syncChannel() {
      const int ch = host_.channel();
      channel_.setText("MIDI CH " + std::to_string(ch));
   }

   void place(int cc, unsigned accent, float x, float y, float w, float h = 100) {
      const int i = indexOfCc(cc);
      if (i < 0)
         return;
      knobs_.push_back({std::make_unique<Knob>(host_, i, accent), x, y, w, h});
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

   void buildLayout() {
      // Header knobs.
      row({1, 70, 7}, kOrange, 880, 2, 270, 88);

      // Oscillators: three groups of shape / tune / fine tune.
      boxes_.push_back({"OSCILLATORS", 12, 84, 780, 160});
      const unsigned oscColors[] = {kOrange, kBlue, kPurple};
      const char *oscNames[] = {"OSC1", "OSC2", "OSC3"};
      for (int o = 0; o < 3; ++o) {
         const float gx = 22 + o * 256.7f;
         headings_.push_back({oscNames[o], oscColors[o], gx, 114, 250});
         row({39 + 3 * o, 40 + 3 * o, 41 + 3 * o}, oscColors[o], gx, 136, 250);
         if (o > 0)
            dividers_.push_back({"", gx - 4, 116, 1, 116});
      }

      boxes_.push_back({"MIX", 12, 256, 340, 136});
      row({48, 49, 50, 51}, kOrange, 22, 286, 320);

      boxes_.push_back({"FILTER", 364, 256, 428, 136});
      row({74, 71, 88, 89, 90}, kOrange, 374, 286, 408);

      // Envelopes: one row each, with the row name on the left.
      boxes_.push_back({"ENVELOPES", 804, 84, 344, 308});
      const unsigned envColors[] = {kOrange, kBlue, kPurple};
      const char *envNames[] = {"ENV1", "ENV2", "ENV3"};
      const int envCcs[3][4] = {{73, 75, 79, 72}, {80, 81, 82, 83}, {84, 85, 86, 87}};
      for (int e = 0; e < 3; ++e) {
         const float y = 112 + e * 92.0f;
         headings_.push_back({envNames[e], envColors[e], 806, y + 36, 56});
         row({envCcs[e][0], envCcs[e][1], envCcs[e][2], envCcs[e][3]}, envColors[e], 862, y, 280, 92);
      }

      boxes_.push_back({"MOD MATRIX", 12, 404, 780, 136});
      row({52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62}, kOrange, 18, 434, 768);

      boxes_.push_back({"LFO", 804, 404, 344, 136});
      row({76, 77, 78}, kOrange, 814, 434, 324);

      boxes_.push_back({"EFFECTS", 12, 552, 628, 148});
      headings_.push_back({"DELAY", kBlue, 22, 576, 258});
      headings_.push_back({"REVERB", kPurple, 290, 576, 258});
      headings_.push_back({"EQ", kGreen, 552, 576, 82});
      row({26, 27, 28}, kBlue, 22, 596, 258);
      row({29, 30, 31}, kPurple, 290, 596, 258);
      place(25, kGreen, 552, 596, 82);
      dividers_.push_back({"", 285, 580, 1, 108});
      dividers_.push_back({"", 550, 580, 1, 108});

      boxes_.push_back({"VOICE", 652, 552, 496, 148});
      row({5, 33, 34, 91, 35, 36, 37}, kOrange, 658, 590, 484);

      // The panels above were laid out under a 72 point title bar; move them below the real one.
      const float shift = kHeaderHeight - 72.0f;
      for (auto &k : knobs_)
         if (k.y > 50)
            k.y += shift;
      for (auto *list : {&boxes_, &dividers_})
         for (Box &b : *list)
            b.y += shift;
      for (SubHeading &h : headings_)
         h.y += shift;
   }

   EditorHost &host_;
   std::vector<PlacedKnob> knobs_;
   std::vector<Box> boxes_, dividers_;
   std::vector<SubHeading> headings_;
   Button chDown_{"<"}, chUp_{">"}, channel_{"MIDI CH 1"}, sendAll_{"SEND ALL"};
};

} // namespace

struct Editor::Impl {
   visage::ApplicationWindow app;
   Panel panel;

   explicit Impl(EditorHost &host) : panel(host) {}
};

Editor::Editor(EditorHost &host) : impl_(std::make_unique<Impl>(host)) {
   auto &app = impl_->app;
   app.addChild(&impl_->panel);
   app.onResize() += [this] { impl_->panel.setBounds(impl_->app.localBounds()); };
   app.setWindowDimensions(kDesignWidth, kDesignHeight);
   app.setFixedAspectRatio(true);
   app.setMinimumDimensions(kDesignWidth * 0.5f, kDesignHeight * 0.5f);
   app.onWindowContentsResized() += [this] {
      if (onResizeRequest)
         onResizeRequest();
   };
}

Editor::~Editor() {
   impl_->app.removeChild(&impl_->panel);
   impl_->app.close();
}

void Editor::show(void *parentWindow) { impl_->app.show(parentWindow); }

#if defined(__APPLE__)
uint32_t Editor::width() const { return static_cast<uint32_t>(impl_->app.width()); }
uint32_t Editor::height() const { return static_cast<uint32_t>(impl_->app.height()); }
void Editor::setSize(uint32_t width, uint32_t height) { impl_->app.setWindowDimensions(width, height); }
#else
uint32_t Editor::width() const { return static_cast<uint32_t>(impl_->app.nativeWidth()); }
uint32_t Editor::height() const { return static_cast<uint32_t>(impl_->app.nativeHeight()); }
void Editor::setSize(uint32_t width, uint32_t height) { impl_->app.setNativeWindowDimensions(width, height); }
#endif

void Editor::adjustSize(uint32_t *width, uint32_t *height) const {
   impl_->app.adjustWindowDimensions(width, height, true, true);
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath) {
   visage::ApplicationEditor editor;
   Panel panel(host);
   editor.addChild(&panel);
   editor.onResize() += [&] { panel.setBounds(editor.localBounds()); };
   editor.setWindowless(width, height);
   panel.setBounds(editor.localBounds());
   editor.takeScreenshot().save(pngPath);
   editor.removeChild(&panel);
   return true;
}

int Editor::posixFd() const { return impl_->app.window() ? impl_->app.window()->posixFd() : -1; }

void Editor::processFdEvents() {
   if (impl_->app.window())
      impl_->app.window()->processPluginFdEvents();
}

} // namespace mm2
