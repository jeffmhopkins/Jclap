// JTunnel's panel. The header holds the mode; the panel below swaps with it, since a sender and a
// receiver share almost nothing. Top left is the tunnel (the name to send as, or what to listen to
// and the tunnels found), top right the link: who is connected to whom, a few seconds of history
// and the live readouts. The bottom row is the send format or the receive buffer, and the output.
// 720 x 480 design grid scaled to the window.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/meter.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "params.h"

namespace jtunnel {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = 720.0f;
constexpr float kDesignHeight = 480.0f;
constexpr float kHeaderHeight = 64.0f;

// The house orange for what is live, a yellow for warnings.
constexpr unsigned kAccent = 0xfff28c28;
constexpr unsigned kAmber = 0xffe8c547;
constexpr unsigned kRed = 0xffe5534b;
constexpr unsigned kIdle = 0xff59606a;
constexpr unsigned kDim = 0xff8d939c; // the theme's dimText, for small headings

// The panel's timer, and how much of it the history graph shows.
constexpr int kTickMs = 30;
constexpr int kHistory = 270; // about 8 s

unsigned withAlpha(unsigned color, unsigned alpha) { return (color & 0x00ffffffu) | (alpha << 24); }

// `a` moved `t` of the way to `b`, opaque. For fills drawn as many touching pieces, where a
// translucent colour would show the seams.
unsigned mix(unsigned a, unsigned b, float t) {
   unsigned out = 0xff000000u;
   for (int shift = 0; shift < 24; shift += 8) {
      const float ca = static_cast<float>((a >> shift) & 0xff), cb = static_cast<float>((b >> shift) & 0xff);
      out |= static_cast<unsigned>(std::lround(ca + (cb - ca) * t)) << shift;
   }
   return out;
}

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

// "384 kbps", or "2.30 Mbps" once it is that big.
std::string rateText(double kbps) {
   char buffer[32];
   if (kbps >= 1000.0)
      std::snprintf(buffer, sizeof(buffer), "%.2f Mbps", kbps / 1000.0);
   else
      std::snprintf(buffer, sizeof(buffer), "%.0f kbps", kbps);
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
   void setPlaceholder(std::string placeholder) {
      if (placeholder != placeholder_) {
         placeholder_ = std::move(placeholder);
         redraw();
      }
   }
   void setAccent(unsigned accent) { accent_ = accent; }
   bool editing() const { return editor_ && editor_->isVisible(); }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = height() / 34.0f;
      canvas.setColor(hover_ || editing() ? accent_ : t.panelBorder);
      canvas.roundedRectangle(0, 0, width(), height(), 5 * s);
      canvas.setColor(t.background);
      canvas.roundedRectangle(1.5f * s, 1.5f * s, width() - 3 * s, height() - 3 * s, 4 * s);
      if (editing())
         return;
      // A pencil-ish tick on the right says "click to type".
      canvas.setColor(hover_ ? accent_ : t.dimText);
      const float px = width() - 20 * s, py = height() * 0.5f;
      canvas.segment(px, py + 5 * s, px + 8 * s, py - 3 * s, 1.6f * s, true);
      canvas.segment(px, py + 5 * s, px + 3 * s, py + 5 * s, 1.6f * s, true);
      canvas.setColor(text_.empty() ? t.dimText : t.text);
      canvas.text(text_.empty() ? placeholder_ : text_, ui::font((text_.empty() ? 13 : 16) * s), visage::Font::kLeft,
                  12 * s, 0, width() - 40 * s, height());
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
      const float s = height() / 34.0f;
      editor_->setBounds(6 * s, 4 * s, width() - 12 * s, height() - 8 * s);
      editor_->setFont(ui::font(16 * s));
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
   unsigned accent_ = kAccent;
   bool hover_ = false;
};

// The tunnels found on the network, one row each with its address; click one to listen to it.
// Scrolls with the wheel when there are more than fit. Empty, it says why.
class TunnelList : public visage::Frame, public ui::Scalable {
public:
   std::function<void(const std::string &)> onPick;

   void setScale(float scale) override { scale_ = scale; }

   void setTunnels(std::vector<TunnelEntry> tunnels) {
      tunnels_ = std::move(tunnels);
      scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(tunnels_.size()) - visibleRows()));
      redraw();
   }
   void setSelected(const std::string &name) {
      if (name != selected_) {
         selected_ = name;
         redraw();
      }
   }
   void setSearchAvailable(bool available) {
      if (available != searchAvailable_) {
         searchAvailable_ = available;
         redraw();
      }
   }
   // The animation of the empty list's "searching" dots.
   void tick(int frame) {
      frame_ = frame;
      if (tunnels_.empty())
         redraw();
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale_, rowH = kRowHeight * s;
      canvas.setColor(t.background);
      canvas.roundedRectangle(0, 0, width(), height(), 5 * s);
      if (tunnels_.empty()) {
         const float cy = height() * 0.5f;
         if (searchAvailable_) {
            for (int i = 0; i < 3; ++i) {
               const float a = 0.5f + 0.5f * std::sin((frame_ * 0.15f) - i * 0.9f);
               canvas.setColor(withAlpha(kAccent, static_cast<unsigned>(60 + 160 * a)));
               canvas.circle(width() * 0.5f - 14 * s + i * 11 * s, cy - 18 * s, 6 * s);
            }
            canvas.setColor(t.dimText);
            canvas.text("Searching the network...", ui::font(13 * s), visage::Font::kCenter, 0, cy - 4 * s, width(),
                        18 * s);
            canvas.text("No tunnels yet. Is a sender running?", ui::font(11 * s), visage::Font::kCenter, 0,
                        cy + 14 * s, width(), 16 * s);
         }
         else {
            canvas.setColor(kAmber);
            canvas.text("Network search unavailable", ui::font(13 * s), visage::Font::kCenter, 0, cy - 14 * s,
                        width(), 18 * s);
            canvas.setColor(t.dimText);
            canvas.text("Type the sender's address above,", ui::font(11 * s), visage::Font::kCenter, 0, cy + 4 * s,
                        width(), 16 * s);
            canvas.text("as its panel shows it", ui::font(11 * s), visage::Font::kCenter, 0, cy + 18 * s, width(),
                        16 * s);
         }
         return;
      }
      const int rows = std::min(visibleRows(), static_cast<int>(tunnels_.size()) - scroll_);
      for (int i = 0; i < rows; ++i) {
         const TunnelEntry &e = tunnels_[static_cast<size_t>(scroll_ + i)];
         const float y = 3 * s + i * rowH, h = rowH - 3 * s, x = 3 * s, w = width() - 6 * s;
         const bool on = e.name == selected_, over = scroll_ + i == hover_;
         if (on) {
            canvas.setColor(withAlpha(kAccent, 0x2a));
            canvas.roundedRectangle(x, y, w, h, 4 * s);
            canvas.setColor(withAlpha(kAccent, 0xb0));
            canvas.roundedRectangleBorder(x, y, w, h, 4 * s, 1.0f * s);
         }
         else if (over) {
            canvas.setColor(t.buttonHover);
            canvas.roundedRectangle(x, y, w, h, 4 * s);
         }
         // A dot per tunnel: filled for the one listened to.
         const float d = 8 * s, cx = x + 12 * s, cy = y + h * 0.5f;
         canvas.setColor(on ? kAccent : over ? t.text : kIdle);
         if (on)
            canvas.circle(cx - d * 0.5f, cy - d * 0.5f, d);
         else
            canvas.ring(cx - d * 0.5f, cy - d * 0.5f, d, 1.5f * s);
         canvas.setColor(on ? t.text : withAlpha(t.text, 0xd8));
         canvas.text(e.name, ui::font(14 * s), visage::Font::kLeft, x + 24 * s, y, w * 0.55f, h);
         canvas.setColor(t.dimText);
         canvas.text(e.local ? "this DAW" : e.address, ui::font(11 * s), visage::Font::kRight, x, y, w - 8 * s, h);
      }
      // Where the visible rows are in a longer list.
      const int total = static_cast<int>(tunnels_.size());
      if (total > visibleRows()) {
         const float trackH = height() - 8 * s, barH = trackH * visibleRows() / total;
         canvas.setColor(t.track);
         canvas.roundedRectangle(width() - 4 * s, 4 * s + trackH * scroll_ / total, 2 * s, barH, 1 * s);
      }
   }

   void mouseMove(const visage::MouseEvent &e) override {
      const int row = rowAt(e.position.y);
      if (row != hover_) {
         hover_ = row;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = -1;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const int row = rowAt(e.position.y);
      if (row >= 0 && onPick)
         onPick(tunnels_[static_cast<size_t>(row)].name);
   }
   bool mouseWheel(const visage::MouseEvent &e) override {
      const int most = std::max(0, static_cast<int>(tunnels_.size()) - visibleRows());
      const int next = std::clamp(scroll_ + (e.wheel_delta_y > 0 ? -1 : 1), 0, most);
      if (next != scroll_) {
         scroll_ = next;
         redraw();
      }
      return true;
   }

private:
   static constexpr float kRowHeight = 28.0f;

   int visibleRows() const { return std::max(1, static_cast<int>((height() / scale_ - 3) / kRowHeight)); }
   int rowAt(float y) const {
      const int row = static_cast<int>((y - 3 * scale_) / (kRowHeight * scale_)) + scroll_;
      return row >= scroll_ && row < static_cast<int>(tunnels_.size()) && row < scroll_ + visibleRows() ? row : -1;
   }

   std::vector<TunnelEntry> tunnels_;
   std::string selected_;
   bool searchAvailable_ = true;
   int scroll_ = 0, hover_ = -1, frame_ = 0;
   float scale_ = 1.0f;
};

// The link as a picture: the sending end on the left, the receiving end(s) on the right, and the
// wire between them in the state's colour, with packets running along it while audio flows.
class LinkView : public visage::Frame, public ui::Scalable {
public:
   struct End {
      std::string title, sub;
      bool lit = false;
   };
   struct State {
      End left, right;
      int receivers = -1;     // >= 0: the right end is this many receivers (the sender's view)
      unsigned color = kIdle; // of the wire and the headline
      bool flowing = false;   // packets on the wire
      bool broken = false;    // no socket: the wire is cut
      std::string headline;   // over the wire
      std::string detail;     // under the picture
      unsigned detailColor = 0;
   };

   void setScale(float scale) override { scale_ = scale; }
   void setState(State state) {
      state_ = std::move(state);
      redraw();
   }
   void tick(int frame) {
      frame_ = frame;
      if (state_.flowing || state_.receivers == 0)
         redraw();
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale_;
      const float nodeW = 128 * s, nodeH = 48 * s, cy = nodeH * 0.5f;
      drawEnd(canvas, 0, state_.left, state_.left.lit ? state_.color : kIdle, -1);
      drawEnd(canvas, width() - nodeW, state_.right, state_.right.lit ? state_.color : kIdle, state_.receivers);

      // The wire, with a small arrow head pointing at the receiving end.
      const float x0 = nodeW + 6 * s, x1 = width() - nodeW - 8 * s, mid = (x0 + x1) * 0.5f;
      const unsigned wire = state_.color;
      if (state_.broken) {
         canvas.setColor(withAlpha(wire, 0x90));
         canvas.segment(x0, cy, mid - 12 * s, cy, 2 * s, true);
         canvas.segment(mid + 12 * s, cy, x1, cy, 2 * s, true);
         canvas.setColor(wire);
         canvas.segment(mid - 5 * s, cy - 5 * s, mid + 5 * s, cy + 5 * s, 2 * s, true);
         canvas.segment(mid - 5 * s, cy + 5 * s, mid + 5 * s, cy - 5 * s, 2 * s, true);
      }
      else if (state_.flowing) {
         canvas.setColor(withAlpha(wire, 0x55));
         canvas.segment(x0, cy, x1, cy, 2 * s, true);
         // Packets, evenly spaced, moving to the right.
         const float gap = 18 * s, offset = std::fmod(frame_ * 1.6f * s, gap);
         for (float x = x0 + offset; x < x1 - 4 * s; x += gap) {
            const float fade = std::min({1.0f, (x - x0) / (24 * s), (x1 - x) / (24 * s)});
            canvas.setColor(withAlpha(wire, static_cast<unsigned>(255 * std::max(0.0f, fade))));
            canvas.roundedRectangle(x - 3 * s, cy - 2.5f * s, 6 * s, 5 * s, 2 * s);
         }
      }
      else {
         // Waiting or idle: a dashed wire.
         canvas.setColor(withAlpha(wire, 0x90));
         for (float x = x0; x < x1; x += 10 * s)
            canvas.segment(x, cy, std::min(x + 4 * s, x1), cy, 2 * s, true);
      }
      if (!state_.broken) {
         canvas.setColor(withAlpha(wire, state_.flowing ? 0xff : 0x90));
         canvas.triangle(x1 + 6 * s, cy, x1 - 2 * s, cy - 5 * s, x1 - 2 * s, cy + 5 * s);
      }
      canvas.setColor(wire);
      canvas.text(state_.headline, ui::font(12 * s), visage::Font::kCenter, x0, cy - 24 * s, x1 - x0, 18 * s);

      if (!state_.detail.empty()) {
         canvas.setColor(state_.detailColor ? state_.detailColor : t.dimText);
         canvas.text(state_.detail, ui::font(12 * s), visage::Font::kCenter, 0, nodeH + 6 * s, width(), 18 * s);
      }
   }

private:
   void drawEnd(visage::Canvas &canvas, float x, const End &end, unsigned color, int receivers) {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale_, w = 128 * s, h = 48 * s;
      canvas.setColor(t.background);
      canvas.roundedRectangle(x, 0, w, h, 6 * s);
      canvas.setColor(withAlpha(color, end.lit ? 0xd0 : 0x80));
      canvas.roundedRectangleBorder(x, 0, w, h, 6 * s, 1.5f * s);
      if (receivers > 0) {
         // One small light per receiver, up to what fits, then "+n".
         const int shown = std::min(receivers, 6);
         const float d = 9 * s, step = 18 * s, ox = x + 14 * s;
         for (int i = 0; i < shown; ++i) {
            canvas.setColor(withAlpha(color, 0x40));
            canvas.circle(ox + i * step - 3 * s, 29 * s - 3 * s, d + 6 * s);
            canvas.setColor(color);
            canvas.circle(ox + i * step, 29 * s, d);
         }
         if (receivers > shown) {
            canvas.setColor(t.dimText);
            canvas.text("+" + std::to_string(receivers - shown), ui::font(11 * s), visage::Font::kLeft,
                        ox + shown * step, 24 * s, 30 * s, 18 * s);
         }
         canvas.setColor(t.text);
         canvas.text(end.title, ui::font(13 * s), visage::Font::kLeft, x + 12 * s, 6 * s, w - 20 * s, 16 * s);
         return;
      }
      if (receivers == 0) {
         // Nobody yet: an empty, slowly breathing seat.
         const float a = 0.5f + 0.5f * std::sin(frame_ * 0.08f);
         canvas.setColor(withAlpha(color, static_cast<unsigned>(80 + 120 * a)));
         canvas.ring(x + 12 * s, 29 * s, 9 * s, 1.5f * s);
      }
      canvas.setColor(end.lit ? t.text : withAlpha(t.text, 0xb0));
      canvas.text(end.title, ui::font(14 * s), visage::Font::kLeft, x + 12 * s, 6 * s, w - 20 * s, 18 * s);
      canvas.setColor(t.dimText);
      canvas.text(end.sub, ui::font(11 * s), visage::Font::kLeft, x + (receivers == 0 ? 28 : 12) * s, 26 * s,
                  w - 20 * s, 16 * s);
   }

   State state_;
   int frame_ = 0;
   float scale_ = 1.0f;
};

// The last few seconds as a line. The receiver plots its lead (what LATENCY shows) against the
// target, with a red tick for each lost or late frame; the sender plots the level it sends.
class HistoryGraph : public visage::Frame, public ui::Scalable {
public:
   // A gap in the line, as while nothing is received.
   static constexpr float kNone = -1e9f;

   HistoryGraph() {
      values_.fill(kNone);
      targets_.fill(kNone);
   }
   void setScale(float scale) override { scale_ = scale; }

   void clear() {
      values_.fill(kNone);
      targets_.fill(kNone);
      events_.fill(0);
      redraw();
   }
   void push(float value, float target, int events) {
      head_ = (head_ + 1) % kHistory;
      values_[static_cast<size_t>(head_)] = value;
      targets_[static_cast<size_t>(head_)] = target;
      events_[static_cast<size_t>(head_)] = static_cast<uint8_t>(std::min(events, 255));
      redraw();
   }
   // Lead in ms (true), or level in dB (false).
   void setMilliseconds(bool ms) {
      if (ms != ms_) {
         ms_ = ms;
         clear();
      }
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale_, w = width(), h = height();
      canvas.setColor(t.background);
      canvas.roundedRectangle(0, 0, w, h, 5 * s);

      // The plot area leaves room for the scale on the right and the title on top.
      const float px = 8 * s, py = 20 * s, pw = w - 50 * s, ph = h - py - 8 * s;
      float lo = -60.0f, hi = 0.0f;
      if (ms_) {
         // Room for the target and the biggest lead seen, in steps of 10 ms so it doesn't twitch.
         float most = 10.0f;
         for (int i = 0; i < kHistory; ++i) {
            most = std::max(most, values_[static_cast<size_t>(i)]);
            most = std::max(most, targets_[static_cast<size_t>(i)] * 1.5f);
         }
         lo = 0.0f;
         hi = std::ceil(most / 10.0f) * 10.0f;
      }
      const auto Y = [&](float v) { return py + ph * (1.0f - std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f)); };
      const auto X = [&](int age) { return px + pw * (1.0f - static_cast<float>(age) / (kHistory - 1)); };

      canvas.setColor(t.dimText);
      canvas.text(ms_ ? "BUFFER LEAD" : "LEVEL SENT", ui::font(10 * s), visage::Font::kLeft, px, 4 * s, 100 * s,
                  14 * s);
      canvas.text("LAST 8 S", ui::font(10 * s), visage::Font::kRight, px, 4 * s, pw, 14 * s);
      if (std::all_of(values_.begin(), values_.end(), [](float v) { return v == kNone; })) {
         canvas.text("Nothing received yet", ui::font(12 * s), visage::Font::kCenter, px, py, pw, ph);
         return;
      }

      // Grid: top, middle and bottom, with the scale on the right.
      for (int i = 0; i <= 2; ++i) {
         const float v = lo + (hi - lo) * i / 2.0f, y = Y(v);
         canvas.setColor(i == 0 ? t.panelBorder : withAlpha(t.panelBorder, 0x90));
         canvas.fill(px, y, pw, std::max(1.0f, 1.0f * s));
         char label[24];
         std::snprintf(label, sizeof(label), ms_ ? "%.0f ms" : "%.0f dB", v);
         canvas.setColor(t.dimText);
         canvas.text(label, ui::font(10 * s), visage::Font::kLeft, px + pw + 6 * s, y - 7 * s, 40 * s, 14 * s);
      }

      // Oldest first, so the line joins up left to right.
      const auto at = [&](const std::array<float, kHistory> &a, int age) {
         return a[static_cast<size_t>((head_ - age + kHistory) % kHistory)];
      };
      if (ms_) {
         // The target: a dashed amber line that steps when auto buffer moves it.
         canvas.setColor(withAlpha(kAmber, 0xc0));
         for (int age = kHistory - 1; age > 0; --age) {
            const float a = at(targets_, age), b = at(targets_, age - 1);
            if (a != kNone && b != kNone && (a != b || (age / 3) % 2 == 0))
               canvas.segment(X(age), Y(a), X(age - 1), Y(b), 1.2f * s, true);
         }
      }
      const float floor = Y(lo);
      for (int age = kHistory - 1; age > 0; --age) {
         const float a = at(values_, age), b = at(values_, age - 1);
         if (a == kNone || b == kNone)
            continue;
         const float xa = X(age), xb = X(age - 1), ya = Y(a), yb = Y(b);
         canvas.setColor(mix(t.background, kAccent, 0.16f));
         canvas.fill(xa, std::min(ya, yb), xb - xa + 1.0f, floor - std::min(ya, yb));
         canvas.setColor(kAccent);
         canvas.segment(xa, ya, xb, yb, 1.6f * s, true);
      }
      for (int age = 0; age < kHistory; ++age)
         if (events_[static_cast<size_t>((head_ - age + kHistory) % kHistory)]) {
            canvas.setColor(kRed);
            canvas.roundedRectangle(X(age) - 1 * s, floor - 7 * s, 2 * s, 7 * s, 1 * s);
         }
      if (ms_) {
         // A small key.
         const float kx = px + 118 * s;
         canvas.setColor(kAccent);
         canvas.fill(kx, 11 * s, 10 * s, 2 * s);
         canvas.setColor(t.dimText);
         canvas.text("lead", ui::font(10 * s), visage::Font::kLeft, kx + 14 * s, 4 * s, 40 * s, 14 * s);
         canvas.setColor(kAmber);
         canvas.fill(kx + 46 * s, 11 * s, 4 * s, 2 * s);
         canvas.fill(kx + 52 * s, 11 * s, 4 * s, 2 * s);
         canvas.setColor(t.dimText);
         canvas.text("target", ui::font(10 * s), visage::Font::kLeft, kx + 60 * s, 4 * s, 40 * s, 14 * s);
         canvas.setColor(kRed);
         canvas.fill(kx + 102 * s, 7 * s, 2 * s, 8 * s);
         canvas.setColor(t.dimText);
         canvas.text("dropout", ui::font(10 * s), visage::Font::kLeft, kx + 108 * s, 4 * s, 50 * s, 14 * s);
      }
   }

private:
   std::array<float, kHistory> values_, targets_;
   std::array<uint8_t, kHistory> events_{};
   int head_ = 0;
   bool ms_ = true;
   float scale_ = 1.0f;
};

// A row of readouts: a small caption over each value, split by thin lines.
class Readouts : public visage::Frame, public ui::Scalable {
public:
   static constexpr int kCount = 4;
   struct Item {
      std::string label, value;
      unsigned color = 0;
   };

   void setScale(float scale) override { scale_ = scale; }
   void set(int i, std::string label, std::string value, unsigned color = 0) {
      Item next{std::move(label), std::move(value), color};
      Item &item = items_[static_cast<size_t>(i)];
      if (next.label != item.label || next.value != item.value || next.color != item.color) {
         item = std::move(next);
         redraw();
      }
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale_, cellW = width() / kCount;
      for (int i = 0; i < kCount; ++i) {
         const Item &item = items_[static_cast<size_t>(i)];
         const float x = i * cellW;
         if (i > 0) {
            canvas.setColor(t.panelBorder);
            canvas.fill(x, 4 * s, std::max(1.0f, 1.0f * s), height() - 8 * s);
         }
         canvas.setColor(t.dimText);
         canvas.text(item.label, ui::font(10 * s), visage::Font::kCenter, x, 0, cellW, 14 * s);
         canvas.setColor(item.color ? item.color : t.text);
         canvas.text(item.value, ui::font(17 * s), visage::Font::kCenter, x, 14 * s, cellW, height() - 14 * s);
      }
   }

private:
   std::array<Item, kCount> items_;
   float scale_ = 1.0f;
};

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      // Header: the mode is the first thing to set, so it lives up here, big.
      update_ = &add<ui::UpdateNotice>({224, 19, 240, 26});
      update_->alignRight = true;
      auto &mode = add<ui::SegmentedSwitch>({476, 16, 230, 32}, std::vector<std::string>{"SEND", "RECEIVE"});
      mode.setAccent(kAccent);
      bindings_.bind(mode, kParamMode);
      tips_.add(mode, "Send this track to the network, or play a tunnel from it");

      // The tunnel: its name, or what to listen to and what's out there.
      tunnelSection_ = &section({12, 76, 276, 240}, "");
      nameLabel_ = &add<ui::Label>({28, 110, 244, 14}, "", kDim, visage::Font::kLeft, 0.8f);
      name_ = &add<TextBox>({28, 127, 244, 34});
      name_->onCommit = [this](const std::string &value) {
         if (receiving())
            host_.setListenTarget(value);
         else
            host_.setSendName(value);
         sync();
      };
      foundLabel_ = &add<ui::Label>({28, 174, 160, 14}, "ON THE NETWORK", kDim, visage::Font::kLeft, 0.8f);
      foundCount_ = &add<ui::Label>({188, 174, 84, 14}, "", 0, visage::Font::kRight, 0.8f);
      list_ = &add<TunnelList>({28, 192, 244, 116});
      list_->onPick = [this](const std::string &name) {
         host_.setListenTarget(name);
         sync();
      };
      // The sender's side of the same box: where receivers can find it.
      reachLabel_ = &add<ui::Label>({28, 174, 244, 14}, "REACHABLE AT", kDim, visage::Font::kLeft, 0.8f);
      address_ = &add<ui::Label>({28, 190, 244, 26}, "", kAccent, visage::Font::kLeft, 0.72f);
      const char *hints[] = {"Receivers pick this name from their", "ON THE NETWORK list, or type the",
                             "address if their search can't see it.", "",
                             "Names: letters, digits, spaces, - and _"};
      for (int i = 0; i < 5; ++i)
         hints_[i] = &add<ui::Label>({28, 226.0f + i * 16, 244, 16}, hints[i], 0, visage::Font::kLeft, 0.72f);
      hints_[4]->setColor(ui::defaultTheme().dimText);
      for (int i = 0; i < 3; ++i)
         hints_[i]->setColor(withAlpha(ui::defaultTheme().text, 0xb0));

      // The link: a picture, some history and the numbers.
      section({300, 76, 408, 240}, "LINK");
      link_ = &add<LinkView>({316, 110, 376, 72});
      graph_ = &add<HistoryGraph>({316, 186, 376, 78});
      readouts_ = &add<Readouts>({316, 272, 376, 36});

      // Bottom left: how a sender codes, or how a receiver buffers. Same box, swapped with the mode.
      bottomSection_ = &section({12, 328, 404, 140}, "");
      codec_ = &add<ui::SegmentedSwitch>({28, 372, 180, 30}, std::vector<std::string>{"OPUS", "PCM 24-BIT"});
      codec_->setAccent(kAccent);
      bindings_.bind(*codec_, kParamCodec);
      tips_.add(*codec_, "Opus is the one to use on Wi-Fi; PCM is uncompressed, for wired networks");
      redundant_ = &add<ui::Button>({28, 416, 180, 30}, "REDUNDANT");
      redundant_->setToggle(true);
      redundant_->setAccent(kAccent);
      bindings_.bind(*redundant_, kParamRedundancy);
      tips_.add(*redundant_, "Repeat each frame in the next packet: a single lost packet costs nothing");
      bitrate_ = &knob(kParamBitrate, "BITRATE", kAccent, {222, 358, 96, 104}, 32.0);
      formatInfo_[0] = &add<ui::Label>({324, 372, 80, 14}, "", 0, visage::Font::kLeft, 0.8f);
      formatInfo_[1] = &add<ui::Label>({324, 388, 80, 18}, "", 0, visage::Font::kLeft, 0.8f);
      formatInfo_[2] = &add<ui::Label>({324, 408, 80, 14}, "", 0, visage::Font::kLeft, 0.8f);
      formatInfo_[0]->setColor(ui::defaultTheme().dimText);
      formatInfo_[2]->setColor(ui::defaultTheme().dimText);

      buffer_ = &knob(kParamBuffer, "BUFFER", kAccent, {24, 358, 104, 104}, 1.0);
      tips_.add(*buffer_, "How far ahead of late packets playback stays");
      autoBuffer_ = &add<ui::Button>({144, 374, 100, 30}, "AUTO");
      autoBuffer_->setToggle(true);
      autoBuffer_->setAccent(kAccent);
      bindings_.bind(*autoBuffer_, kParamAutoBuffer);
      tips_.add(*autoBuffer_, "Grow the buffer after dropouts, shrink it slowly when the network is quiet");
      const char *bufferHints[] = {"Grows after a dropout, shrinks", "back after 20 quiet seconds",
                                   "Wired: about 5 ms    Wi-Fi: 20 ms or more"};
      bufferInfo_[0] = &add<ui::Label>({256, 375, 156, 14}, bufferHints[0], 0, visage::Font::kLeft, 0.8f);
      bufferInfo_[1] = &add<ui::Label>({256, 389, 156, 14}, bufferHints[1], 0, visage::Font::kLeft, 0.8f);
      bufferInfo_[2] = &add<ui::Label>({144, 422, 260, 16}, bufferHints[2], 0, visage::Font::kLeft, 0.75f);
      for (ui::Label *l : bufferInfo_)
         l->setColor(ui::defaultTheme().dimText);

      // Bottom right: what comes out.
      section({428, 328, 280, 140}, "OUTPUT");
      level_ = &knob(kParamLevel, "LEVEL", kAccent, {440, 358, 96, 104}, 1.0);
      tips_.add(*level_, "Level of what this receiver plays");
      thru_ = &add<ui::Button>({548, 384, 84, 30}, "THRU");
      thru_->setToggle(true);
      thru_->setAccent(kAccent);
      bindings_.bind(*thru_, kParamThru);
      thruInfo_ = &add<ui::Label>({548, 418, 90, 14}, "", 0, visage::Font::kLeft, 0.8f);
      thruInfo_->setColor(ui::defaultTheme().dimText);
      add<ui::Label>({642, 358, 26, 14}, "IN", 0, visage::Font::kCenter, 0.75f);
      add<ui::Label>({670, 358, 26, 14}, "OUT", 0, visage::Font::kCenter, 0.75f);
      inMeter_ = &add<ui::Meter>({648, 374, 14, 82});
      outMeter_ = &add<ui::Meter>({676, 374, 14, 82});

      sync();
      startTimer(kTickMs);
   }

   void timerCallback() override { sync(); }

   // ui::Section isn't Scalable, so its title would stay at design size when the panel is zoomed.
   void resized() override {
      DesignFrame::resized();
      for (ui::Section *section : sections_)
         section->setScale(scale());
   }

   // Runs the timer's work `ticks` times straight away, so an offscreen render shows a few seconds
   // of history instead of an empty graph.
   void simulate(int ticks) {
      for (int i = 0; i < ticks; ++i)
         sync();
   }

   void draw(visage::Canvas &canvas) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      canvas.setColor(t.background);
      canvas.fill(0, 0, width(), height());

      canvas.setColor(t.headerBar);
      canvas.fill(0, 0, width(), kHeaderHeight * s);
      drawLogo(canvas, s);
      canvas.setColor(kAccent);
      canvas.text("J", ui::font(28 * s), visage::Font::kLeft, 58 * s, 12 * s, 30 * s, 40 * s);
      canvas.setColor(t.text);
      canvas.text("Tunnel", ui::font(28 * s), visage::Font::kLeft, 71 * s, 12 * s, 120 * s, 40 * s);
      if (!update_->isVisible()) {
         canvas.setColor(t.dimText);
         canvas.text("audio and MIDI over the network", ui::font(13 * s), visage::Font::kLeft, 172 * s, 15 * s,
                     260 * s, 40 * s);
      }
   }

private:
   void drawLogo(visage::Canvas &canvas, float s) {
      // A tunnel mouth: an arch with a dot travelling through it.
      canvas.setColor(kAccent);
      canvas.roundedRectangleBorder(14 * s, 14 * s, 36 * s, 36 * s, 16 * s, 3.0f * s);
      canvas.setColor(ui::defaultTheme().headerBar);
      canvas.fill(12 * s, 38 * s, 40 * s, 14 * s);
      canvas.setColor(kAccent);
      canvas.fill(14 * s, 38 * s, 3 * s, 10 * s);
      canvas.fill(47 * s, 38 * s, 3 * s, 10 * s);
      canvas.setColor(kAmber);
      canvas.circle(27 * s, 27 * s, 10 * s);
   }

   ui::Section &section(ui::Rect bounds, const char *title) {
      auto &added = add<ui::Section>(bounds, title);
      added.setScale(scale());
      sections_.push_back(&added);
      return added;
   }

   ui::Knob &knob(clap_id id, const char *label, unsigned accent, ui::Rect cell, double step) {
      const ParamInfo &p = kParams[id];
      auto &k = add<ui::Knob>(cell, ui::ValueRange{p.min, p.max, p.def, step, false, p.stepped});
      k.setLabel(label);
      k.setAccent(accent);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      return k;
   }

   bool receiving() const { return std::lround(host_.paramValue(kParamMode)) == kModeReceive; }

   // Shows the widgets of one mode and hides the other's.
   void applyMode(bool rx) {
      tunnelSection_->setTitle(rx ? "LISTEN TO" : "SEND AS");
      nameLabel_->setText(rx ? "TUNNEL NAME OR ADDRESS" : "TUNNEL NAME");
      name_->setPlaceholder(rx ? "Name, or 192.168.1.20:47000" : "Name this tunnel");
      for (visage::Frame *f : std::initializer_list<visage::Frame *>{foundLabel_, foundCount_, list_})
         f->setVisible(rx);
      for (visage::Frame *f : std::initializer_list<visage::Frame *>{reachLabel_, address_})
         f->setVisible(!rx);
      for (ui::Label *l : hints_)
         l->setVisible(!rx);

      bottomSection_->setTitle(rx ? "RECEIVE" : "SEND FORMAT");
      for (visage::Frame *f : std::initializer_list<visage::Frame *>{codec_, redundant_, bitrate_, formatInfo_[0],
                                                                     formatInfo_[1], formatInfo_[2]})
         f->setVisible(!rx);
      for (visage::Frame *f :
           std::initializer_list<visage::Frame *>{buffer_, autoBuffer_, bufferInfo_[0], bufferInfo_[1], bufferInfo_[2]})
         f->setVisible(rx);

      // The level is the receiver's; a sender's output is only its input passed through.
      level_->setVisible(rx);
      move(*thru_, rx ? ui::Rect{548, 384, 84, 30} : ui::Rect{448, 384, 120, 30});
      move(*thruInfo_, rx ? ui::Rect{548, 418, 90, 14} : ui::Rect{448, 418, 180, 14});
      thruInfo_->setText(rx ? "mix input in" : "keep playing it here");
      tips_.setText(*thru_, rx ? "Mix this track's own input in with what is received"
                               : "Keep playing this track's input here as well as sending it");

      graph_->setMilliseconds(rx);
      lastLost_ = lastConcealed_ = 0;
      names_.clear();
      listTicks_ = 0;
   }

   void sync() {
      const bool rx = receiving();
      if (rx != shownRx_ || !modeShown_) {
         shownRx_ = rx;
         modeShown_ = true;
         applyMode(rx);
      }
      ++frame_;
      if (!name_->editing())
         name_->setText(rx ? host_.listenTarget() : host_.sendName());

      // The network list changes slowly; refresh it every second or so.
      const TunnelStatus st = host_.status();
      if (rx && (++listTicks_ >= 30 || names_.empty())) {
         listTicks_ = 0;
         std::vector<TunnelEntry> tunnels = host_.tunnels();
         std::vector<std::string> names;
         for (const TunnelEntry &e : tunnels)
            names.push_back(e.name + e.address);
         if (names != names_ || names.empty()) {
            names_ = names;
            char count[24];
            std::snprintf(count, sizeof(count), tunnels.empty() ? "" : "%d found", static_cast<int>(tunnels.size()));
            foundCount_->setText(count);
            list_->setTunnels(std::move(tunnels));
         }
      }
      if (rx) {
         list_->setSelected(host_.listenTarget());
         list_->setSearchAvailable(!st.active || st.discoveryOnNetwork);
         list_->tick(frame_);
      }
      else
         address_->setText(st.localAddress.empty() ? "-" : st.localAddress);

      if (rx)
         syncReceiver(st);
      else
         syncSender(st);
      link_->tick(frame_);

      inMeter_->setLevel(st.inputPeak);
      outMeter_->setLevel(st.outputPeak);
   }

   // The common states before there is anything to show: switched off, or no socket.
   bool offline(const TunnelStatus &st, LinkView::State &link) {
      if (!st.active) {
         link.headline = "OFF";
         link.detail = "Not running: the host has the plugin switched off";
         return true;
      }
      if (!st.linkUp) {
         link.headline = "NO NETWORK";
         link.color = kRed;
         link.broken = true;
         link.detail = "Couldn't open a UDP socket";
         link.detailColor = kRed;
         return true;
      }
      return false;
   }

   void syncSender(const TunnelStatus &st) {
      LinkView::State link;
      link.left = {host_.sendName().empty() ? "(no name)" : host_.sendName(), "this track", true};
      link.receivers = st.listeners;
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), st.listeners == 1 ? "1 receiver" : "%d receivers", st.listeners);
      link.right = {st.listeners > 0 ? buffer : "Nobody yet", st.listeners > 0 ? "" : "waiting", st.listeners > 0};
      const bool opus = std::lround(host_.paramValue(kParamCodec)) == kCodecOpus;
      const bool redundant = host_.paramValue(kParamRedundancy) >= 0.5;
      const double rate = opus ? 48000.0 : st.hostRate;
      // What one receiver costs: the codec's rate, twice over with redundancy. Packet headers aside.
      const double kbps =
         (opus ? host_.paramValue(kParamBitrate) : rate * 24.0 * 2.0 / 1000.0) * (redundant ? 2.0 : 1.0);

      if (!offline(st, link)) {
         if (st.listeners > 0) {
            link.headline = "SENDING";
            link.color = kAccent;
            link.flowing = true;
            std::snprintf(buffer, sizeof(buffer), "%s, %.1f kHz%s", opus ? "Opus" : "PCM 24-bit", rate / 1000.0,
                          redundant ? ", redundant" : "");
            link.detail = buffer;
         }
         else {
            link.headline = "READY";
            link.color = kAmber;
            link.detail = "Nobody is listening yet";
            link.detailColor = kAmber;
         }
         if (!st.discoveryOnNetwork) {
            link.detail = "Network search unavailable: receivers must type the address";
            link.detailColor = kAmber;
         }
         // A test setting nobody should forget is on.
         if (!st.impairment.empty()) {
            link.detail = "Test impairment: " + st.impairment;
            link.detailColor = kAmber;
         }
         // Another sender had this name first: we aren't announced, so nobody can find us by it.
         if (!st.nameTakenBy.empty()) {
            link.headline = "NAME IN USE";
            link.color = kRed;
            link.broken = true;
            link.flowing = false;
            link.detail = "Already sent from " + st.nameTakenBy + ": give this one another name";
            link.detailColor = kRed;
         }
      }
      link_->setState(std::move(link));

      // The level going out, as a line. Silence floors it at the bottom of the scale.
      const float db = st.inputPeak > 0.0f ? 20.0f * std::log10(st.inputPeak) : -60.0f;
      graph_->push(std::max(db, -60.0f), 0.0f, 0);

      std::snprintf(buffer, sizeof(buffer), "%d", st.listeners);
      readouts_->set(0, "RECEIVERS", buffer, st.listeners > 0 ? kAccent : 0);
      readouts_->set(1, "FORMAT", opus ? "Opus" : "PCM 24");
      std::snprintf(buffer, sizeof(buffer), "%.1f kHz", rate / 1000.0);
      readouts_->set(2, "RATE", rate > 0 ? buffer : "-");
      readouts_->set(3, "UPLOAD", st.listeners > 0 ? rateText(kbps * st.listeners) : "-");

      // The bitrate knob only means something for Opus; PCM's rate is fixed by the host.
      bitrate_->setVisible(opus);
      move(*formatInfo_[0], opus ? ui::Rect{330, 382, 76, 14} : ui::Rect{226, 382, 180, 14});
      move(*formatInfo_[1], opus ? ui::Rect{330, 398, 76, 18} : ui::Rect{226, 398, 180, 18});
      move(*formatInfo_[2], opus ? ui::Rect{330, 420, 76, 14} : ui::Rect{226, 420, 180, 14});
      formatInfo_[0]->setText(opus ? "PER RECEIVER" : "UNCOMPRESSED, PER RECEIVER");
      formatInfo_[1]->setText(rateText(kbps));
      formatInfo_[2]->setText(opus ? "5 ms frames" : "2 ms frames: for wired networks");
   }

   void syncReceiver(const TunnelStatus &st) {
      LinkView::State link;
      const std::string target = host_.listenTarget();
      link.left = {target.empty() ? "(nothing)" : target, st.source.empty() ? "sender" : st.source, false};
      link.right = {"This track", st.localAddress.empty() ? "receiver" : st.localAddress, false};
      char buffer[160];
      const bool playing = st.connection == 3 && st.state == 2;
      if (!offline(st, link)) {
         switch (st.connection) {
         case 1:
            link.headline = "LOOKING";
            link.color = kAmber;
            link.left.sub = "not found yet";
            std::snprintf(buffer, sizeof(buffer), "Looking for \"%s\" on the network...", target.c_str());
            link.detail = buffer;
            break;
         case 2:
            link.headline = "CONNECTING";
            link.color = kAmber;
            link.left.lit = true;
            std::snprintf(buffer, sizeof(buffer), "Asking %s for audio...", st.source.c_str());
            link.detail = buffer;
            break;
         case 3:
            link.headline = playing ? "RECEIVING" : "BUFFERING";
            link.color = playing ? kAccent : kAmber;
            link.flowing = true;
            link.left.lit = link.right.lit = true;
            if (st.sampleRate) {
               std::snprintf(buffer, sizeof(buffer), "%s, %.1f kHz%s", st.codec == 0 ? "Opus" : "PCM 24-bit",
                             st.sampleRate / 1000.0, playing ? "" : ", filling the buffer");
               link.detail = buffer;
            }
            break;
         default:
            link.headline = "IDLE";
            link.detail = "Pick a tunnel from the list, or type its name";
         }
         if (!st.discoveryOnNetwork && st.connection != 3) {
            link.detail = "Network search unavailable: type the sender's address";
            link.detailColor = kAmber;
         }
      }
      link_->setState(std::move(link));

      // Each new lost or late frame becomes a tick on the graph.
      const bool live = st.state != 0;
      const int events = static_cast<int>((st.lost - std::min(st.lost, lastLost_)) +
                                          (st.concealed - std::min(st.concealed, lastConcealed_)));
      const bool counted = lastLost_ || lastConcealed_ || frame_ > 1;
      lastLost_ = st.lost;
      lastConcealed_ = st.concealed;
      if (live)
         graph_->push(st.fillMs, st.targetMs, counted ? events : 0);
      else
         graph_->push(HistoryGraph::kNone, HistoryGraph::kNone, 0);

      // The lead goes amber when it has fallen well under the target, as it does in a stall.
      char value[48];
      std::snprintf(value, sizeof(value), "%.0f ms", st.fillMs);
      readouts_->set(0, "LATENCY", live ? value : "-", live && st.fillMs < st.targetMs * 0.5f ? kAmber : 0);
      std::snprintf(value, sizeof(value), "%.0f ms", st.targetMs);
      readouts_->set(1, "TARGET", live ? value : "-");
      if (std::isnan(st.driftPpm))
         std::snprintf(value, sizeof(value), "...");
      else
         std::snprintf(value, sizeof(value), "%+.0f ppm", st.driftPpm);
      readouts_->set(2, "CLOCK", live ? value : "-");
      std::snprintf(value, sizeof(value), "%u / %u", st.lost, st.concealed);
      readouts_->set(3, "LOST / LATE", st.connection == 3 ? value : "-", st.lost + st.concealed ? kAmber : 0);
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::UpdateNotice *update_ = nullptr;
   ui::Section *tunnelSection_ = nullptr, *bottomSection_ = nullptr;
   ui::Label *nameLabel_ = nullptr, *foundLabel_ = nullptr, *foundCount_ = nullptr, *reachLabel_ = nullptr;
   ui::Label *address_ = nullptr, *thruInfo_ = nullptr;
   ui::Label *hints_[5] = {}, *formatInfo_[3] = {}, *bufferInfo_[3] = {};
   TextBox *name_ = nullptr;
   TunnelList *list_ = nullptr;
   LinkView *link_ = nullptr;
   HistoryGraph *graph_ = nullptr;
   Readouts *readouts_ = nullptr;
   ui::SegmentedSwitch *codec_ = nullptr;
   ui::Button *redundant_ = nullptr, *autoBuffer_ = nullptr, *thru_ = nullptr;
   ui::Knob *bitrate_ = nullptr, *buffer_ = nullptr, *level_ = nullptr;
   ui::Meter *inMeter_ = nullptr, *outMeter_ = nullptr;
   std::vector<ui::Section *> sections_;
   std::vector<std::string> names_;
   uint32_t lastLost_ = 0, lastConcealed_ = 0;
   int listTicks_ = 0, frame_ = 0;
   bool shownRx_ = false, modeShown_ = false;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.simulate(kHistory);
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jtunnel
