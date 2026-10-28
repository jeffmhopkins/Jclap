// JTunnel's panel: the tunnel (send or receive, and its name), the send format, the receive
// buffer with live readouts of the stream, and levels. 720 x 440 design grid scaled to the window.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/led.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/update_notice.h>

#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "params.h"

namespace jtunnel {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 440.0f;
constexpr float kHeaderHeight = 64.0f;

constexpr unsigned kTeal = 0xff3fc1b0;
constexpr unsigned kAmber = 0xfff2b13c;
constexpr unsigned kRed = 0xffe5534b;

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

// A one-line text box: shows its text, and opens an editor on click. Enter or a click elsewhere
// keeps the edit, Escape drops it.
class TextBox : public visage::Frame {
public:
   std::function<void(const std::string &)> onCommit;

   void setText(const std::string &value) {
      if (value != text_) {
         text_ = value;
         redraw();
      }
   }
   void setPlaceholder(std::string placeholder) { placeholder_ = std::move(placeholder); }
   void setAccent(unsigned accent) { accent_ = accent; }
   bool editing() const { return editor_ && editor_->isVisible(); }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = height() / 30.0f;
      canvas.setColor(hover_ || editing() ? accent_ : t.panelBorder);
      canvas.roundedRectangle(0, 0, width(), height(), 4 * s);
      canvas.setColor(t.button);
      canvas.roundedRectangle(1.5f * s, 1.5f * s, width() - 3 * s, height() - 3 * s, 3 * s);
      if (editing())
         return;
      canvas.setColor(text_.empty() ? t.dimText : t.text);
      canvas.text(text_.empty() ? placeholder_ : text_, ui::font(14 * s), visage::Font::kLeft, 10 * s, 0,
                  width() - 20 * s, height());
   }

   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &) override { startEditing(); }

private:
   class Editor : public visage::TextEditor {
   public:
      std::function<void()> onClickAway;
      void focusChanged(bool is_focused, bool was_clicked) override {
         visage::TextEditor::focusChanged(is_focused, was_clicked);
         if (!is_focused && onClickAway)
            onClickAway();
      }
   };

   void startEditing() {
      if (!editor_) {
         editor_ = std::make_unique<Editor>();
         editor_->setMaxCharacters(63);
         editor_->onEnterKey() += [this] { finish(true); };
         editor_->onEscapeKey() += [this] { finish(false); };
         editor_->onClickAway = [this] { finish(true); };
         addChild(editor_.get(), false);
      }
      const float s = height() / 30.0f;
      editor_->setBounds(4 * s, 3 * s, width() - 8 * s, height() - 6 * s);
      editor_->setFont(ui::font(14 * s));
      editor_->setText(text_);
      editor_->setVisible(true);
      editor_->requestKeyboardFocus();
      editor_->selectAll();
      redraw();
   }

   void finish(bool apply) {
      if (!editing())
         return;
      editor_->setVisible(false);
      if (apply) {
         std::string value = editor_->text().toUtf8();
         for (char &ch : value)
            if (ch == '\n' || ch == '\r' || ch == '\t')
               ch = ' ';
         text_ = value;
         if (onCommit)
            onCommit(value);
      }
      redraw();
   }

   std::unique_ptr<Editor> editor_;
   std::string text_, placeholder_;
   unsigned accent_ = kTeal;
   bool hover_ = false;
};

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      update_ = &add<ui::UpdateNotice>({480, 19, 220, 26});
      update_->alignRight = true;
      add<ui::Section>({12, 76, 456, 172}, "TUNNEL");
      add<ui::Section>({480, 76, 228, 172}, "SEND FORMAT");
      add<ui::Section>({12, 260, 456, 168}, "RECEIVE");
      add<ui::Section>({480, 260, 228, 168}, "LEVELS");

      // The tunnel: send or receive, and which one.
      auto &mode = add<ui::SegmentedSwitch>({28, 104, 200, 32}, std::vector<std::string>{"SEND", "RECEIVE"});
      mode.setAccent(kTeal);
      bindings_.bind(mode, kParamMode);
      nameLabel_ = &add<ui::Label>({28, 146, 260, 16}, "", kTeal, visage::Font::kLeft);
      name_ = &add<TextBox>({28, 164, 260, 32});
      name_->onCommit = [this](const std::string &value) {
         if (receiving())
            host_.setListenTarget(value);
         else
            host_.setSendName(value);
         sync();
      };
      found_ = &add<ui::Dropdown>({300, 164, 152, 32});
      found_->setAccent(kTeal);
      found_->onChange = [this](int index) {
         if (index > 0 && index <= static_cast<int>(names_.size())) {
            host_.setListenTarget(names_[static_cast<size_t>(index - 1)]);
            sync();
         }
      };
      foundLabel_ = &add<ui::Label>({300, 146, 152, 16}, "ON THE NETWORK", kTeal, visage::Font::kLeft);
      status_ = &add<ui::Label>({28, 206, 424, 18}, "", 0, visage::Font::kLeft, 0.72f);
      detail_ = &add<ui::Label>({28, 224, 424, 16}, "", 0, visage::Font::kLeft, 0.72f);
      led_ = &add<ui::ActivityLed>({252, 108, 200, 24});
      led_->fadeMs = 0;

      // How the sender codes the audio.
      auto &codec = add<ui::SegmentedSwitch>({496, 104, 196, 32}, std::vector<std::string>{"OPUS", "PCM 24"});
      codec.setAccent(kAmber);
      bindings_.bind(codec, kParamCodec);
      knob(kParamBitrate, "BITRATE", kAmber, {492, 144, 96, 96}, 32.0);
      auto &redundant = add<ui::Button>({596, 176, 98, 30}, "REDUNDANT");
      redundant.setAccent(kAmber);
      bindings_.bind(redundant, kParamRedundancy);

      // How the receiver buffers, and what it's seeing.
      knob(kParamBuffer, "BUFFER", kTeal, {24, 288, 100, 100}, 1.0);
      auto &autoBuffer = add<ui::Button>({132, 322, 84, 30}, "AUTO");
      autoBuffer.setAccent(kTeal);
      bindings_.bind(autoBuffer, kParamAutoBuffer);
      knob(kParamLevel, "LEVEL", kTeal, {224, 288, 100, 100}, 1.0);
      const char *readoutNames[] = {"LATENCY", "TARGET", "CLOCK", "LOST / LATE"};
      for (int i = 0; i < 4; ++i) {
         add<ui::Label>({334, 296.0f + i * 28, 70, 16}, readoutNames[i], 0, visage::Font::kLeft, 0.7f);
         readouts_[i] = &add<ui::Label>({404, 295.0f + i * 28, 58, 18}, "-", 0, visage::Font::kRight, 0.8f);
      }

      // Levels.
      add<ui::Label>({500, 288, 34, 14}, "IN", 0, visage::Font::kCenter, 0.8f);
      add<ui::Label>({546, 288, 34, 14}, "OUT", 0, visage::Font::kCenter, 0.8f);
      inMeter_ = &add<ui::Meter>({508, 306, 18, 108});
      outMeter_ = &add<ui::Meter>({554, 306, 18, 108});
      auto &thru = add<ui::Button>({596, 345, 98, 30}, "THRU");
      thru.setAccent(kTeal);
      bindings_.bind(thru, kParamThru);

      sync();
      startTimer(30);
   }

   void timerCallback() override { sync(); }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      canvas.setColor(t.background);
      canvas.fill(0, 0, width(), height());

      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kTeal);
      canvas.text("J", ui::font(28 * s), visage::Font::kLeft, 58 * s, 12 * s, 30 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("Tunnel", ui::font(28 * s), visage::Font::kLeft, 71 * s, 12 * s, 120 * s, 40 * s);
      canvas.setColor(t.dimText);
      if (!update_->isVisible())
         canvas.text("Audio and MIDI between JTunnels on the network", ui::font(13 * s), visage::Font::kRight,
                     300 * s, 12 * s, 400 * s, 40 * s);
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A tunnel mouth: an arch with a dot travelling through it.
      canvas.setColor(kTeal);
      canvas.roundedRectangleBorder(14 * s, 14 * s, 36 * s, 36 * s, 16 * s, 3.0f * s);
      canvas.setColor(ui::defaultTheme().headerBar);
      canvas.fill(12 * s, 38 * s, 40 * s, 14 * s);
      canvas.setColor(kTeal);
      canvas.fill(14 * s, 38 * s, 3 * s, 10 * s);
      canvas.fill(47 * s, 38 * s, 3 * s, 10 * s);
      canvas.setColor(kAmber);
      canvas.circle(27 * s, 27 * s, 10 * s);
   }

   void knob(clap_id id, const char *label, unsigned accent, ui::Rect cell, double step) {
      const ParamInfo &p = kParams[id];
      auto &k = add<ui::Knob>(cell, ui::ValueRange{p.min, p.max, p.def, step, false, p.stepped});
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
   }

   bool receiving() const { return std::lround(host_.paramValue(kParamMode)) == kModeReceive; }

   void sync() {
      const bool rx = receiving();
      nameLabel_->setText(rx ? "LISTEN TO (NAME OR ADDRESS)" : "SEND AS");
      name_->setPlaceholder(rx ? "Tunnel name, or 192.168.1.20:47000" : "Tunnel name");
      if (!name_->editing())
         name_->setText(rx ? host_.listenTarget() : host_.sendName());
      found_->setVisible(rx);
      foundLabel_->setVisible(rx);

      // The network list changes slowly; refresh it every second or so.
      if (rx && (++listTicks_ >= 30 || names_.empty())) {
         listTicks_ = 0;
         const std::vector<std::string> names = host_.tunnelNames();
         if (names != names_ || found_->options().empty()) {
            names_ = names;
            std::vector<std::string> options;
            char head[48];
            std::snprintf(head, sizeof(head), names_.empty() ? "None found" : "%d found", static_cast<int>(names_.size()));
            options.push_back(head);
            options.insert(options.end(), names_.begin(), names_.end());
            found_->setOptions(options);
         }
         const std::string target = host_.listenTarget();
         const auto it = std::find(names_.begin(), names_.end(), target);
         found_->setSelected(it == names_.end() ? 0 : static_cast<int>(it - names_.begin()) + 1);
      }

      const TunnelStatus st = host_.status();
      std::string line, more;
      unsigned light = 0xff3a3f47;
      float level = 1.0f;
      if (!st.active) {
         line = "Not running (the host has the plugin switched off)";
      }
      else if (!st.linkUp) {
         line = "No network: couldn't open a UDP socket";
         light = kRed;
      }
      else if (!rx) {
         char buffer[160];
         std::snprintf(buffer, sizeof(buffer), "Reachable at %s", st.localAddress.c_str());
         line = buffer;
         if (st.listeners > 0) {
            std::snprintf(buffer, sizeof(buffer), "Sending to %d listener%s", st.listeners, st.listeners == 1 ? "" : "s");
            light = kTeal;
         }
         else {
            std::snprintf(buffer, sizeof(buffer), "Nobody listening yet");
            level = 0.5f;
            light = kAmber;
         }
         more = buffer;
      }
      else {
         char buffer[160];
         const std::string target = host_.listenTarget();
         switch (st.connection) {
         case 1:
            std::snprintf(buffer, sizeof(buffer), "Looking for \"%s\" on the network...", target.c_str());
            light = kAmber;
            level = 0.5f;
            break;
         case 2:
            std::snprintf(buffer, sizeof(buffer), "Asking %s for audio...", st.source.c_str());
            light = kAmber;
            break;
         case 3:
            std::snprintf(buffer, sizeof(buffer), "Receiving from %s", st.source.c_str());
            light = kTeal;
            break;
         default:
            std::snprintf(buffer, sizeof(buffer), "Pick a tunnel to listen to");
         }
         line = buffer;
         if (st.sampleRate) {
            std::snprintf(buffer, sizeof(buffer), "%s, %.1f kHz%s", st.codec == 0 ? "Opus" : "PCM 24-bit",
                          st.sampleRate / 1000.0, st.state == 1 ? ", buffering" : "");
            more = buffer;
         }
      }
      if (st.active && !st.discoveryOnNetwork)
         more += more.empty() ? "Network search unavailable: type an address" : "  (network search unavailable)";
      status_->setText(line);
      detail_->setText(more);
      led_->setColor(light);
      led_->setLevel(level);
      led_->setLabel(!st.active ? "OFF" : !rx ? (st.listeners > 0 ? "SENDING" : "READY") : st.connection == 3 ? "RECEIVING" : "WAITING");

      char value[48];
      const bool live = rx && st.state != 0;
      std::snprintf(value, sizeof(value), "%.0f ms", st.fillMs);
      readouts_[0]->setText(live ? value : "-");
      std::snprintf(value, sizeof(value), "%.0f ms", st.targetMs);
      readouts_[1]->setText(live ? value : "-");
      std::snprintf(value, sizeof(value), "%+.0f ppm", st.driftPpm);
      readouts_[2]->setText(live ? value : "-");
      std::snprintf(value, sizeof(value), "%u / %u", st.lost, st.concealed);
      readouts_[3]->setText(rx ? value : "-");

      inMeter_->setLevel(st.inputPeak);
      outMeter_->setLevel(st.outputPeak);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::UpdateNotice *update_ = nullptr;
   ui::Label *nameLabel_ = nullptr, *foundLabel_ = nullptr, *status_ = nullptr, *detail_ = nullptr;
   ui::Label *readouts_[4] = {};
   TextBox *name_ = nullptr;
   ui::Dropdown *found_ = nullptr;
   ui::ActivityLed *led_ = nullptr;
   ui::Meter *inMeter_ = nullptr, *outMeter_ = nullptr;
   std::vector<std::string> names_;
   int listTicks_ = 0;
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

} // namespace jtunnel
