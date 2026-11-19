// JKit's panel, on a 1080 x 660 design grid scaled to the window, in JArp's plain style: a title bar
// with the two tabs and the kit browser, and cards under it.
//
//    GENERATE   MODEL (the engine, its download and its size on disk), PROMPT (the words, the role,
//               the random prompt and its GENERIC - STRANGE slider, length, steps, guidance, variety,
//               seed and how many variants), VARIANTS (what came back), SELECTED (trim, fade,
//               normalize, then which pad it goes on)
//    KIT        PADS (4 x 4, pad 1 bottom left), the selected pad's sound, EQ and sends, and under a
//               darker band the master: EQ, reverb and delay returns, and OUTPUT (stereo field, levels,
//               width, boom, volume)
//
// Colour carries meaning: orange is the sound and the selected thing; purple the reverb; blue the
// delay and the fade; each role has its own colour for its dot.

#include "editor.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/presets.h>
#include <jclap/ui/section.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <visage/widgets.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "prompts.h"

namespace jkit {
namespace {

namespace ui = jclap::ui;

constexpr float kDesignWidth = kEditorWidth, kDesignHeight = kEditorHeight;
constexpr float kHeaderHeight = 64.0f;
// The top of the Kit tab's master band.
constexpr float kMasterTop = 520.0f;

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff8fd1a8;
constexpr unsigned kYellow = 0xffe8c66a;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kSlate = 0xff454c58;
constexpr unsigned kRule = 0xff2c323b;
constexpr unsigned kCell = 0xff272c34;
constexpr unsigned kEmpty = 0xff1f232a;
constexpr unsigned kWell = 0xff181b20;
constexpr unsigned kTitle = 0xffc3c7cd;
constexpr unsigned kFaint = 0xff5d636c;
constexpr unsigned kOffWave = 0xff6a717b;

const ui::PresetFormat kKitFormat{"JKit", 1, ".jkit", kParamCount};

unsigned roleColour(int role) {
   static const unsigned c[kRoleCount] = {kOrange, kBlue, kPurple, kBlue, kGreen, kGreen, kYellow};
   return c[std::clamp(role, 0, kRoleCount - 1)];
}

unsigned alpha(unsigned c, float a) {
   return (static_cast<unsigned>(std::clamp(a, 0.0f, 1.0f) * ((c >> 24) & 0xff)) << 24) | (c & 0xffffff);
}

std::string text(clap_id id, double value) {
   char buffer[32];
   formatParam(id, value, buffer, sizeof(buffer));
   return buffer;
}

std::string bytesText(uint64_t b) {
   char buffer[32];
   if (b >= 1000000000ull)
      std::snprintf(buffer, sizeof(buffer), "%.2f GB", b / 1e9);
   else if (b >= 1000000ull)
      std::snprintf(buffer, sizeof(buffer), "%.0f MB", b / 1e6);
   else if (b > 0)
      std::snprintf(buffer, sizeof(buffer), "%.0f kB", std::max(1.0, b / 1e3));
   else
      std::snprintf(buffer, sizeof(buffer), "nothing");
   return buffer;
}

std::string secondsText(double s) {
   char buffer[32];
   std::snprintf(buffer, sizeof(buffer), s < 10 ? "%.2f s" : "%.1f s", s);
   return buffer;
}

std::string agoText(int64_t when) {
   const int64_t d = std::max<int64_t>(0, static_cast<int64_t>(std::time(nullptr)) - when);
   char buffer[32];
   if (d < 60)
      return "now";
   if (d < 3600)
      std::snprintf(buffer, sizeof(buffer), "%d min", static_cast<int>(d / 60));
   else if (d < 86400)
      std::snprintf(buffer, sizeof(buffer), "%d h", static_cast<int>(d / 3600));
   else
      std::snprintf(buffer, sizeof(buffer), "%d d", static_cast<int>(d / 86400));
   return buffer;
}

std::string ellipsis(const std::string &s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "\xE2\x80\xA6"; }

// A mirrored bar waveform from overview peaks, with the bars outside [from, to] dimmed.
void drawWave(visage::Canvas &c, const std::vector<float> &peaks, float x, float y, float w, float h, unsigned colour,
              double from = 0.0, double to = 1.0) {
   const size_t n = peaks.size();
   if (!n)
      return;
   const float bw = w / n, mid = y + h / 2;
   // Peaks are scaled so the loudest fills the height: quiet sounds still read.
   float top = 1e-4f;
   for (float p : peaks)
      top = std::max(top, p);
   for (size_t i = 0; i < n; ++i) {
      const double t = (i + 0.5) / n;
      const float a = std::max(0.5f, peaks[i] / top * (h / 2 - 1));
      c.setColor(t < from || t > to ? alpha(colour, 0.25f) : colour);
      c.fill(x + i * bw, mid - a, std::max(0.8f, bw - 0.8f), 2 * a);
   }
   c.setColor(alpha(colour, 0.45f));
   c.fill(x, mid - 0.25f, w, 0.5f);
}

// Peaks of a sample, remembered until the sample changes.
struct WaveCache {
   const Sample *sample = nullptr;
   int bins = 0;
   std::vector<float> peaks;
   const std::vector<float> &get(const SamplePtr &s, int n) {
      if (s.get() != sample || n != bins) {
         sample = s.get();
         bins = n;
         peaks = s ? overview(*s, n) : std::vector<float>();
      }
      return peaks;
   }
};

// A card: its panel, title (and a note on the right), and the rule under the title.
void drawCard(visage::Canvas &c, float s, float x, float y, float w, float h, const std::string &title,
              const std::string &note = {}, unsigned titleColour = kTitle) {
   const ui::Theme &t = ui::defaultTheme();
   c.setColor(t.panel);
   c.roundedRectangle(x * s, y * s, w * s, h * s, 6 * s);
   c.setColor(t.panelBorder);
   c.roundedRectangleBorder(x * s, y * s, w * s, h * s, 6 * s, 1.0f);
   if (!title.empty()) {
      c.setColor(titleColour);
      c.text(title, ui::font(11 * s), visage::Font::kLeft, (x + 12) * s, (y + 8) * s, (w - 24) * s, 16 * s);
   }
   if (!note.empty()) {
      c.setColor(alpha(t.dimText, 0.75f));
      c.text(note, ui::font(9 * s), visage::Font::kRight, (x + 12) * s, (y + 8) * s, (w - 24) * s, 16 * s);
   }
   c.setColor(kRule);
   c.fill((x + 12) * s, (y + 28) * s, (w - 24) * s, 1.0f * s);
}

void drawLabel(visage::Canvas &c, float s, const std::string &l, float x, float y, float w = 160,
               unsigned colour = 0, visage::Font::Justification j = visage::Font::kLeft) {
   c.setColor(colour ? colour : ui::defaultTheme().dimText);
   c.text(l, ui::font(9.5f * s), j, x * s, y * s, w * s, 14 * s);
}

// ---- small widgets ----

class CheckBox : public visage::Frame {
public:
   explicit CheckBox(std::string label) : label_(std::move(label)) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const float box = 11, y = (height() / s - box) / 2;
      if (on_) {
         c.setColor(kSlate);
         c.roundedRectangle(0, y * s, box * s, box * s, 2.5f * s);
         c.setColor(kOrange);
         c.roundedRectangle(3 * s, (y + 3) * s, (box - 6) * s, (box - 6) * s, 1.5f * s);
      }
      c.setColor(t.dimText);
      c.roundedRectangleBorder(0, y * s, box * s, box * s, 2.5f * s, 1.0f * s);
      c.setColor(on_ ? t.text : t.dimText);
      c.text(label_, ui::font(9.5f * s), visage::Font::kLeft, (box + 6) * s, 0, width() - (box + 6) * s, height());
   }
   void mouseDown(const visage::MouseEvent &) override {
      on_ = !on_;
      redraw();
      if (onToggle)
         onToggle(on_);
   }
   void setOn(bool on) {
      if (on != on_) {
         on_ = on;
         redraw();
      }
   }
   bool on() const { return on_; }
   std::function<void(bool)> onToggle;

private:
   std::string label_;
   bool on_ = false;
};

// A square button with a drawn icon: the dice, the lock.
class IconButton : public visage::Frame {
public:
   enum class Icon { Dice, Lock };
   explicit IconButton(Icon icon) : icon_(icon) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(lit ? kSlate : hover_ ? t.buttonHover : t.button);
      c.roundedRectangle(0, 0, width(), height(), 4 * s);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 4 * s, 1.0f);
      const float cx = width() / 2, cy = height() / 2;
      if (icon_ == Icon::Dice) {
         c.setColor(kOrange);
         ui::drawDie(c, cx, cy, 13 * s);
      } else {
         c.setColor(lit ? kOrange : t.dimText);
         c.roundedRectangle(cx - 4.5f * s, cy - 1 * s, 9 * s, 7 * s, 1.5f * s);
         c.ring(cx - 3.2f * s, cy - 6.5f * s, 6.4f * s, 1.3f * s);
         c.setColor(lit ? kSlate : hover_ ? t.buttonHover : t.button);
         if (!lit)
            c.fill(cx + 1.2f * s, cy - 3.8f * s, 2.4f * s, 2.6f * s);
      }
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
   bool lit = false;
   std::function<void()> onClick;

private:
   Icon icon_;
   bool hover_ = false;
};

// A wide button in the accent: GENERATE, ADD TO KIT. While `progress` is >= 0 it fills with it.
class GoButton : public visage::Frame {
public:
   explicit GoButton(std::string label) : label_(std::move(label)) {}
   void setLabel(const std::string &l) {
      if (l != label_) {
         label_ = l;
         redraw();
      }
   }
   void setEnabled(bool e) {
      if (e != enabled_) {
         enabled_ = e;
         redraw();
      }
   }
   void setProgress(double p) {
      if (p != progress_) {
         progress_ = p;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float r = 6 * s;
      if (progress_ > -2.0) {
         c.setColor(0xff3a3226);
         c.roundedRectangle(0, 0, width(), height(), r);
         if (progress_ >= 0) {
            c.setColor(kOrange);
            c.roundedRectangle(0, 0, std::max(2 * r, static_cast<float>(width() * progress_)), height(), r);
         }
      } else {
         c.setColor(!enabled_ ? 0xff3a3226u : hover_ ? 0xfff59a3fu : kOrange);
         c.roundedRectangle(0, 0, width(), height(), r);
      }
      c.setColor(enabled_ || progress_ > -2.0 ? 0xff1b1408u : 0xff7a6a55u);
      if (progress_ > -2.0 && progress_ < 0.5)
         c.setColor(0xffe6e8ebu);
      c.text(label_, ui::font(std::min(13.0f, height() / s * 0.36f) * s), visage::Font::kCenter, 0, 0, width(),
             height());
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
   std::function<void()> onClick;

private:
   std::string label_;
   bool enabled_ = true, hover_ = false;
   double progress_ = -3.0; // < -2: not running
};

// The role chips: one is on.
class ChipRow : public visage::Frame {
public:
   static float chipWidth(int i) { return std::string(kRoleNames[i]).size() * 6.6f + 18.0f; }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      float x = 0;
      for (int i = 0; i < kRoleCount; ++i) {
         const float w = chipWidth(i);
         c.setColor(i == selected ? kSlate : t.button);
         c.roundedRectangle(x * s, 0, w * s, height(), height() / 2);
         c.setColor(t.panelBorder);
         c.roundedRectangleBorder(x * s, 0, w * s, height(), height() / 2, 1.0f);
         std::string name = kRoleNames[i];
         for (char &ch : name)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
         c.setColor(i == selected ? t.text : t.dimText);
         c.text(name, ui::font(9 * s), visage::Font::kCenter, x * s, 0, w * s, height());
         x += w + 5;
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      float x = 0;
      for (int i = 0; i < kRoleCount; ++i) {
         const float w = chipWidth(i);
         if (e.position.x >= x * s && e.position.x < (x + w) * s) {
            selected = i;
            redraw();
            if (onChange)
               onChange(i);
            return;
         }
         x += w + 5;
      }
   }
   int selected = 0;
   std::function<void(int)> onChange;
};

// A plain horizontal slider with a word at each end: GENERIC ... STRANGE.
class EndsSlider : public ui::ValueControl {
public:
   EndsSlider(std::string left, std::string right)
      : ValueControl(ui::ValueRange{0.0, 1.0, 0.3, 0.05}), left_(std::move(left)), right_(std::move(right)) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const float side = 56 * s, x0 = side, x1 = width() - side, cy = height() / 2;
      c.setColor(t.dimText);
      c.text(left_, ui::font(9 * s), visage::Font::kLeft, 0, 0, side, height());
      c.text(right_, ui::font(9 * s), visage::Font::kRight, x1, 0, side, height());
      c.setColor(t.track);
      c.roundedRectangle(x0, cy - 1.5f * s, x1 - x0, 3 * s, 1.5f * s);
      const float f = fraction(value());
      c.setColor(kOrange);
      c.roundedRectangle(x0, cy - 1.5f * s, (x1 - x0) * f, 3 * s, 1.5f * s);
      c.circle(x0 + (x1 - x0) * f - 6 * s, cy - 6 * s, 12 * s);
      c.setColor(t.panel);
      c.circle(x0 + (x1 - x0) * f - 2.5f * s, cy - 2.5f * s, 5 * s);
   }

protected:
   float dragDistance(visage::Point from, visage::Point to) const override { return to.x - from.x; }
   float dragLength() const override { return width() - 112 * ui::designScale(*this); }

private:
   std::string left_, right_;
};

// The prompt: a multi-line text box.
class PromptBox : public visage::Frame {
public:
   PromptBox() {
      editor_.setMultiLine(true);
      editor_.setJustification(visage::Font::kTopLeft);
      editor_.setMaxCharacters(300);
      editor_.setDefaultText("Describe the sound, or roll the dice for one");
      editor_.onTextChange() += [this] {
         if (onChange)
            onChange(editor_.text().toUtf8());
      };
      addChild(&editor_);
   }
   void resized() override {
      const float s = ui::designScale(*this);
      editor_.setBounds(6 * s, 4 * s, width() - 12 * s, height() - 8 * s);
      editor_.setFont(ui::font(11.5f * s));
      editor_.setMargin(4 * s, 4 * s);
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(ui::defaultTheme().knobDark);
      c.roundedRectangle(0, 0, width(), height(), 5 * s);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 5 * s, 1.0f);
   }
   void setText(const std::string &t) {
      if (editor_.text().toUtf8() != t)
         editor_.setText(t);
   }
   std::function<void(const std::string &)> onChange;

private:
   visage::TextEditor editor_;
};

// ---- the Generate tab ----

// The variants that came back: 1 to 8 tiles, each with its waveform, seed, length and star. Click
// one to select it (and hear it), click its star to keep it through the next generation.
class VariantGrid : public visage::Frame {
public:
   explicit VariantGrid(EditorHost &host) : host_(host) {}

   void layout(int n, int &cols, int &rows) const {
      cols = n <= 1 ? 1 : n <= 4 ? 2 : 4;
      rows = n <= 2 ? 1 : 2;
      if (n == 2) {
         cols = 2;
         rows = 1;
      }
   }
   visage::Bounds tile(int i, int n) const {
      int cols, rows;
      layout(std::max(n, 4), cols, rows);
      const float s = ui::designScale(*this), g = 8 * s;
      const float w = (width() - g * (cols - 1)) / cols, h = (height() - g * (rows - 1)) / rows;
      return {(i % cols) * (w + g), (i / cols) * (h + g), w, h};
   }

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const auto &vars = host_.variants();
      const int n = static_cast<int>(vars.size());
      const int shown = std::max(n <= 4 ? 4 : 8, n);
      caches_.resize(static_cast<size_t>(std::max(n, 8)));
      const float playing = host_.auditionPosition();
      for (int i = 0; i < std::min(shown, 8); ++i) {
         const visage::Bounds b = tile(i, shown);
         if (i >= n) {
            c.setColor(0xff1a1d22);
            c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), 5 * s);
            c.setColor(t.panelBorder);
            c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), 5 * s, 1.0f);
            if (i == 0) {
               c.setColor(kFaint);
               c.text(emptyText, ui::font(10 * s), visage::Font::kCenter, b.x(), b.y(), b.width(), b.height());
            }
            continue;
         }
         const Variant &v = vars[static_cast<size_t>(i)];
         const bool sel = i == host_.selectedVariant;
         c.setColor(t.knobDark);
         c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), 5 * s);
         c.setColor(sel ? kOrange : t.panelBorder);
         c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), 5 * s, sel ? 1.5f * s : 1.0f);
         const std::string letter(1, static_cast<char>('A' + i));
         c.setColor(sel ? kOrange : t.text);
         c.text(letter, ui::font(11 * s), visage::Font::kLeft, b.x() + 10 * s, b.y() + 7 * s, 20 * s, 14 * s);
         c.setColor(t.dimText);
         c.text("seed " + std::to_string(v.sample->seed), ui::font(9 * s), visage::Font::kLeft, b.x() + 26 * s,
                b.y() + 7 * s, 100 * s, 14 * s);
         c.text(secondsText(v.sample->seconds()), ui::font(9 * s), visage::Font::kRight, b.x() + b.width() - 80 * s,
                b.y() + 7 * s, 54 * s, 14 * s);
         c.setColor(v.starred ? kOrange : 0xff6b717a);
         ui::drawStar(c, b.x() + b.width() - 14 * s, b.y() + 14 * s, 6 * s, v.starred);
         const float wx = b.x() + 10 * s, wy = b.y() + 26 * s, ww = b.width() - 20 * s, wh = b.height() - 34 * s;
         const int bins = std::max(8, static_cast<int>(ww / (2.2f * s)));
         drawWave(c, caches_[static_cast<size_t>(i)].get(v.sample, bins), wx, wy, ww, wh, sel ? kOrange : kOffWave);
         if (sel && playing >= 0.0f && auditioning == v.sample.get()) {
            c.setColor(0xd8ffffff);
            c.fill(wx + ww * playing, wy - 2 * s, 1.5f * s, wh + 4 * s);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const auto &vars = host_.variants();
      const int n = static_cast<int>(vars.size());
      const int shown = std::max(n <= 4 ? 4 : 8, n);
      const float s = ui::designScale(*this);
      for (int i = 0; i < n; ++i) {
         const visage::Bounds b = tile(i, shown);
         if (!b.contains(e.position))
            continue;
         if (e.position.x > b.x() + b.width() - 26 * s && e.position.y < b.y() + 26 * s) {
            host_.variants()[static_cast<size_t>(i)].starred = !vars[static_cast<size_t>(i)].starred;
         } else {
            host_.selectedVariant = i;
            if (onSelect)
               onSelect(i);
         }
         redraw();
         return;
      }
   }

   std::string emptyText = "Variants appear here";
   const Sample *auditioning = nullptr;
   std::function<void(int)> onSelect;

private:
   EditorHost &host_;
   std::vector<WaveCache> caches_;
};

// The selected variant, with its start and end handles and the fade before the end (drag the blue
// curve's top sideways to set it). The parts that will be cut are dimmed.
class TrimView : public visage::Frame {
public:
   explicit TrimView(EditorHost &host) : host_(host) {}
   SamplePtr sample;
   float playhead = -1.0f;

   float xOf(double f) const {
      const float s = ui::designScale(*this);
      return 6 * s + static_cast<float>(f) * (width() - 12 * s);
   }
   double fOf(float x) const {
      const float s = ui::designScale(*this);
      return std::clamp((x - 6 * s) / (width() - 12 * s), 0.0f, 1.0f);
   }

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(kWell);
      c.roundedRectangle(0, 0, width(), height(), 4 * s);
      c.setColor(kRule);
      c.roundedRectangleBorder(0, 0, width(), height(), 4 * s, 1.0f);
      if (!sample) {
         c.setColor(kFaint);
         c.text("Nothing generated yet", ui::font(10 * s), visage::Font::kCenter, 0, 0, width(), height());
         return;
      }
      const Treatment &t = host_.treatment();
      const int bins = std::max(16, static_cast<int>((width() - 12 * s) / (3.4f * s)));
      drawWave(c, cache_.get(sample, bins), 6 * s, 6 * s, width() - 12 * s, height() - 12 * s, kOrange, t.start, t.end);
      // The fade: a dashed quarter cosine down to the end handle.
      const double secs = sample->seconds();
      const float xe = xOf(t.end);
      if (t.fadeMs > 0.5 && secs > 0) {
         const float xf = xOf(std::max(t.start, t.end - t.fadeMs / 1000.0 / secs));
         c.setColor(kBlue);
         const int steps = 24;
         for (int i = 0; i < steps; i += 2) {
            const float a = static_cast<float>(i) / steps, b = static_cast<float>(i + 1) / steps;
            const auto y = [&](float u) { return 4 * s + (height() - 8 * s) * (1.0f - std::cos(u * 1.5707963f)); };
            c.segment(xf + (xe - xf) * a, y(a), xf + (xe - xf) * b, y(b), 1.3f * s, true);
         }
         c.circle(xf - 3 * s, 4 * s - 3 * s, 6 * s);
      }
      // The handles.
      for (int i = 0; i < 2; ++i) {
         const float x = xOf(i ? t.end : t.start);
         c.setColor(kWhite);
         c.fill(x - 1 * s, 2 * s, 2 * s, height() - 4 * s);
         c.roundedRectangle(i ? x - 9 * s : x - 1 * s, 2 * s, 10 * s, 10 * s, 3 * s);
      }
      if (playhead >= 0) {
         c.setColor(0xd8ffffff);
         c.fill(xOf(playhead), 2 * s, 1.5f * s, height() - 4 * s);
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      if (!sample)
         return;
      const Treatment &t = host_.treatment();
      const float s = ui::designScale(*this);
      const float xs = xOf(t.start), xe = xOf(t.end);
      const double secs = sample->seconds();
      const float xf = xOf(std::max(t.start, t.end - t.fadeMs / 1000.0 / std::max(1e-3, secs)));
      drag_ = 0;
      if (e.position.y < 16 * s && std::fabs(e.position.x - xf) < 8 * s && t.fadeMs > 0.5)
         drag_ = 3;
      else if (std::fabs(e.position.x - xs) < 8 * s)
         drag_ = 1;
      else if (std::fabs(e.position.x - xe) < 8 * s)
         drag_ = 2;
      else if (e.position.y < 16 * s && e.position.x < xe && e.position.x > xs)
         drag_ = 3; // grab the top to pull a fade out of the end
      else if (onClick)
         onClick(fOf(e.position.x));
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (!sample || !drag_)
         return;
      Treatment &t = host_.treatment();
      const double f = fOf(e.position.x);
      const double minGap = 0.01 / std::max(1e-3, sample->seconds());
      if (drag_ == 1)
         t.start = std::min(f, t.end - minGap);
      else if (drag_ == 2)
         t.end = std::max(f, t.start + minGap);
      else
         t.fadeMs = std::clamp((t.end - std::max(f, t.start)) * sample->seconds() * 1000.0, 0.0, 2000.0);
      redraw();
      if (onEdit)
         onEdit();
   }
   void mouseUp(const visage::MouseEvent &) override { drag_ = 0; }

   std::function<void()> onEdit;
   std::function<void(double)> onClick;

private:
   EditorHost &host_;
   WaveCache cache_;
   int drag_ = 0;
};

// The 4 x 4 pads as a small picker, laid out like the Kit tab (pad 1 bottom left).
class PadPicker : public visage::Frame {
public:
   explicit PadPicker(EditorHost &host) : host_(host) {}
   visage::Bounds cell(int i) const {
      const float s = ui::designScale(*this), g = 4 * s;
      const float w = (width() - 3 * g) / 4, h = (height() - 3 * g) / 4;
      return {(i % 4) * (w + g), (3 - i / 4) * (h + g), w, h};
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      for (int i = 0; i < kPads; ++i) {
         const visage::Bounds b = cell(i);
         const SamplePtr p = host_.padSample(i);
         const bool on = i == host_.targetPad;
         c.setColor(on ? kSlate : p ? kCell : kEmpty);
         c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), 3 * s);
         c.setColor(on ? kOrange : t.panelBorder);
         c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), 3 * s, on ? 1.2f * s : 1.0f);
         if (p) {
            c.setColor(roleColour(p->role));
            c.circle(b.x() + 5 * s, b.y() + b.height() / 2 - 2 * s, 4 * s);
         }
         c.setColor(on ? t.text : p ? kTitle : kFaint);
         const std::string name = p ? ellipsis(p->name, 10) : std::to_string(i + 1) + " \xC2\xB7 empty";
         c.text(name, ui::font(8.5f * s), visage::Font::kLeft, b.x() + (p ? 12 : 5) * s, b.y(), b.width() - 14 * s,
                b.height());
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      for (int i = 0; i < kPads; ++i)
         if (cell(i).contains(e.position)) {
            host_.targetPad = i;
            redraw();
            if (onPick)
               onPick(i);
         }
   }
   std::function<void(int)> onPick;

private:
   EditorHost &host_;
};

// ---- the Kit tab ----

// The 16 pads: name, role dot, waveform with the live playhead, note, and the reverb and delay sends
// as two small bars. Click to select and play; drag onto another pad to swap them; right-click for
// more. An empty pad offers to generate a sound for it.
class PadGrid : public visage::Frame {
public:
   explicit PadGrid(EditorHost &host) : host_(host) {}
   visage::Bounds cell(int i) const {
      const float s = ui::designScale(*this), g = 6 * s;
      const float w = (width() - 3 * g) / 4, h = (height() - 3 * g) / 4;
      return {(i % 4) * (w + g), (3 - i / 4) * (h + g), w, h};
   }
   int at(visage::Point p) const {
      for (int i = 0; i < kPads; ++i)
         if (cell(i).contains(p))
            return i;
      return -1;
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      for (int i = 0; i < kPads; ++i) {
         const visage::Bounds b = cell(i);
         const SamplePtr p = host_.padSample(i);
         const bool sel = i == host_.selectedPad, target = dragging_ && i == hover_ && i != dragFrom_;
         const std::string note = ui::noteName(kFirstNote + i);
         if (!p) {
            c.setColor(kEmpty);
            c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), 5 * s);
            c.setColor(sel ? kOrange : target ? kWhite : 0xff3a404a);
            // Dashed: short runs along each side.
            const float dash = 4 * s;
            for (float x = b.x() + 4 * s; x < b.right() - 4 * s; x += 2 * dash) {
               c.fill(x, b.y(), dash, 1.0f);
               c.fill(x, b.bottom() - 1.0f, dash, 1.0f);
            }
            for (float y = b.y() + 4 * s; y < b.bottom() - 4 * s; y += 2 * dash) {
               c.fill(b.x(), y, 1.0f, dash);
               c.fill(b.right() - 1.0f, y, 1.0f, dash);
            }
            c.setColor(0xff4a515c);
            c.text(std::to_string(i + 1), ui::font(8 * s), visage::Font::kLeft, b.x() + 6 * s, b.y() + 5 * s, 20 * s,
                   12 * s);
            c.text(note, ui::font(8 * s), visage::Font::kLeft, b.x() + 6 * s, b.bottom() - 16 * s, 30 * s, 12 * s);
            c.setColor(kFaint);
            c.text("+ GENERATE", ui::font(8.5f * s), visage::Font::kCenter, b.x(), b.y(), b.width(), b.height());
            continue;
         }
         c.setColor(sel ? 0xff2c2620 : kCell);
         c.roundedRectangle(b.x(), b.y(), b.width(), b.height(), 5 * s);
         c.setColor(sel ? kOrange : target ? kWhite : t.panelBorder);
         c.roundedRectangleBorder(b.x(), b.y(), b.width(), b.height(), 5 * s, sel || target ? 1.5f * s : 1.0f);
         const float level = host_.padLevel(i);
         c.setColor(roleColour(p->role));
         c.circle(b.x() + 6 * s, b.y() + 9 * s, 5 * s);
         if (level > 0.01f) {
            c.setColor(ui::steadyGlow(alpha(roleColour(p->role), std::min(1.0f, level * 1.5f)),
                                      {b.x() + 8.5f * s, b.y() + 11.5f * s}, 9 * s));
            c.circle(b.x() - 0.5f * s, b.y() + 2.5f * s, 18 * s);
         }
         c.setColor(sel ? t.text : kTitle);
         c.text(ellipsis(p->name, 14), ui::font(9.5f * s), visage::Font::kLeft, b.x() + 15 * s, b.y() + 5 * s,
                b.width() - 20 * s, 13 * s);
         const float wx = b.x() + 6 * s, wy = b.y() + 22 * s, ww = b.width() - 12 * s, wh = b.height() - 42 * s;
         caches_[i].get(p, 30);
         drawWave(c, caches_[i].peaks, wx, wy, ww, wh, sel ? kOrange : 0xff5f6670);
         const float pos = host_.padPosition(i);
         if (pos >= 0.0f) {
            c.setColor(0xe6ffffff);
            c.fill(wx + ww * pos, wy - 2 * s, 1.5f * s, wh + 4 * s);
         }
         c.setColor(t.dimText);
         c.text(note, ui::font(8 * s), visage::Font::kLeft, b.x() + 6 * s, b.bottom() - 16 * s, 30 * s, 12 * s);
         // The sends.
         const float bw = 18 * s, bx = b.right() - 6 * s - 2 * bw - 3 * s, by = b.bottom() - 10 * s;
         const double rv = host_.paramValue(padParam(i, kReverbSend)), dl = host_.paramValue(padParam(i, kDelaySend));
         c.setColor(t.track);
         c.fill(bx, by, bw, 2 * s);
         c.fill(bx + bw + 3 * s, by, bw, 2 * s);
         c.setColor(kPurple);
         c.fill(bx, by, bw * static_cast<float>(rv), 2 * s);
         c.setColor(kBlue);
         c.fill(bx + bw + 3 * s, by, bw * static_cast<float>(dl), 2 * s);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const int i = at(e.position);
      if (i < 0)
         return;
      if (e.shouldTriggerPopup()) {
         if (onMenu)
            onMenu(i, e.position);
         return;
      }
      dragFrom_ = hover_ = i;
      dragging_ = false;
      if (host_.padSample(i)) {
         const visage::Bounds b = cell(i);
         // Higher on the pad is louder, as on a pad controller.
         const float vel = std::clamp(1.0f - (e.position.y - b.y()) / b.height() * 0.6f, 0.3f, 1.0f);
         host_.playPad(i, vel);
         playing_ = i;
      }
      if (onSelect)
         onSelect(i);
      redraw();
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      const int i = at(e.position);
      if (dragFrom_ >= 0 && i != dragFrom_)
         dragging_ = true;
      if (i != hover_) {
         hover_ = i;
         redraw();
      }
   }
   void mouseUp(const visage::MouseEvent &e) override {
      if (playing_ >= 0)
         host_.stopPad(playing_);
      playing_ = -1;
      const int i = at(e.position);
      if (dragging_ && dragFrom_ >= 0 && i >= 0 && i != dragFrom_) {
         host_.swapPads(dragFrom_, i);
         host_.selectedPad = i;
         if (onSelect)
            onSelect(i);
      } else if (!dragging_ && i >= 0 && !host_.padSample(i) && onEmpty) {
         onEmpty(i);
      }
      dragging_ = false;
      dragFrom_ = hover_ = -1;
      redraw();
   }

   std::function<void(int)> onSelect, onEmpty;
   std::function<void(int, visage::Point)> onMenu;

private:
   EditorHost &host_;
   WaveCache caches_[kPads];
   int dragFrom_ = -1, hover_ = -1, playing_ = -1;
   bool dragging_ = false;
};

// The selected pad's sound: its waveform with the Start and End handles (dragging them sets the
// params), the decay as a dashed curve from the start, the playhead, and the speed Pitch gives it.
class PadWave : public visage::Frame {
public:
   explicit PadWave(EditorHost &host) : host_(host) {}
   float xOf(double f) const {
      const float s = ui::designScale(*this);
      return 6 * s + static_cast<float>(f) * (width() - 12 * s);
   }
   double fOf(float x) const {
      const float s = ui::designScale(*this);
      return std::clamp((x - 6 * s) / (width() - 12 * s), 0.0f, 1.0f);
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const int pad = host_.selectedPad;
      c.setColor(kWell);
      c.roundedRectangle(0, 0, width(), height(), 4 * s);
      c.setColor(kRule);
      c.roundedRectangleBorder(0, 0, width(), height(), 4 * s, 1.0f);
      const SamplePtr p = host_.padSample(pad);
      if (!p) {
         c.setColor(kFaint);
         c.text("Empty pad", ui::font(10 * s), visage::Font::kCenter, 0, 0, width(), height());
         return;
      }
      const double a = host_.paramModulated(padParam(pad, kStart)), z = host_.paramModulated(padParam(pad, kEnd));
      const bool rev = host_.paramValue(padParam(pad, kReverse)) >= 0.5;
      drawWave(c, cache_.get(p, 84), 6 * s, 4 * s, width() - 12 * s, height() - 8 * s, kOrange, a, z);
      // The decay from where it starts playing, over the region's real length.
      const double decay = host_.paramModulated(padParam(pad, kDecay));
      if (!decayFull(decay)) {
         const double pitch = host_.paramModulated(padParam(pad, kPitch));
         const double speed = std::pow(2.0, pitch / 12.0);
         const double seconds = p->seconds();
         const double ms = decayMs(decay);
         c.setColor(kBlue);
         const float x0 = xOf(rev ? z : a), dir = rev ? -1.0f : 1.0f;
         const int steps = 40;
         for (int i = 0; i < steps; i += 2) {
            const auto pt = [&](int k, float &x, float &y) {
               const double tms = ms * k / steps; // ms of output
               const double f = tms / 1000.0 * speed / seconds;
               x = x0 + dir * static_cast<float>(f) * (width() - 12 * s);
               const double g = std::pow(0.001, static_cast<double>(k) / steps);
               y = 3 * s + (height() - 6 * s) * static_cast<float>(1.0 - g);
            };
            float xa, ya, xb, yb;
            pt(i, xa, ya);
            pt(i + 1, xb, yb);
            if ((dir > 0 && xa > xOf(z)) || (dir < 0 && xa < xOf(a)))
               break;
            c.segment(xa, ya, xb, yb, 1.2f * s, true);
         }
      }
      for (int i = 0; i < 2; ++i) {
         const float x = xOf(i ? z : a);
         c.setColor(kWhite);
         c.fill(x - 1 * s, 1 * s, 2 * s, height() - 2 * s);
         c.roundedRectangle(i ? x - 8 * s : x - 1 * s, 1 * s, 9 * s, 9 * s, 3 * s);
      }
      const float pos = host_.padPosition(pad);
      if (pos >= 0) {
         c.setColor(0xd8ffffff);
         c.fill(xOf(pos), 1 * s, 1.5f * s, height() - 2 * s);
      }
      const double pitch = host_.paramModulated(padParam(pad, kPitch));
      if (std::fabs(pitch) > 0.01) {
         char buffer[32];
         std::snprintf(buffer, sizeof(buffer), "\xC3\x97%.2f speed", std::pow(2.0, pitch / 12.0));
         c.setColor(kBlue);
         c.text(buffer, ui::font(8.5f * s), visage::Font::kRight, width() - 86 * s, 4 * s, 80 * s, 12 * s);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const int pad = host_.selectedPad;
      if (!host_.padSample(pad))
         return;
      const float s = ui::designScale(*this);
      const float xa = xOf(host_.paramValue(padParam(pad, kStart))), xz = xOf(host_.paramValue(padParam(pad, kEnd)));
      drag_ = std::fabs(e.position.x - xa) <= std::fabs(e.position.x - xz) ? kStart : kEnd;
      if (std::min(std::fabs(e.position.x - xa), std::fabs(e.position.x - xz)) > 14 * s) {
         drag_ = -1;
         host_.playPad(pad, 0.9f);
         playing_ = true;
         return;
      }
      id_ = padParam(pad, static_cast<PadParam>(drag_));
      host_.beginEdit(id_);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (drag_ < 0)
         return;
      const int pad = host_.selectedPad;
      double f = fOf(e.position.x);
      if (drag_ == kStart)
         f = std::min(f, host_.paramValue(padParam(pad, kEnd)) - 0.002);
      else
         f = std::max(f, host_.paramValue(padParam(pad, kStart)) + 0.002);
      host_.setParam(id_, f);
      redraw();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (drag_ >= 0)
         host_.endEdit(id_);
      if (playing_)
         host_.stopPad(host_.selectedPad);
      playing_ = false;
      drag_ = -1;
   }

private:
   EditorHost &host_;
   WaveCache cache_;
   int drag_ = -1;
   clap_id id_ = 0;
   bool playing_ = false;
};

// An EQ's response: low cut, low shelf, mid bell, high shelf (and the master's fixed cuts), on a log
// axis from 20 Hz to 20 kHz, with a dot on each band.
class EqView : public visage::Frame {
public:
   struct Bands {
      double lowCut = 0, low = 0, mid = 0, midHz = 1000, high = 0, highCut = 0;
   };
   std::function<Bands()> bands;
   unsigned colour = kOrange;
   bool labels = true;

   static double response(const Bands &b, double f) {
      double d = 0;
      const auto sig = [](double z) { return 1.0 / (1.0 + std::exp(-z)); };
      if (b.lowCut > 0)
         d -= 10 * std::log10(1 + std::pow(b.lowCut / f, 4));
      if (b.highCut > 0)
         d -= 10 * std::log10(1 + std::pow(f / b.highCut, 4));
      d += b.low * (1 - sig(std::log2(f / 120.0) * 2.2));
      d += b.high * sig(std::log2(f / 7000.0) * 2.2);
      d += b.mid * std::exp(-std::pow(std::log2(f / b.midHz), 2) / (2 * 0.8 * 0.8));
      return std::clamp(d, -18.0, 18.0);
   }

   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float w = width(), h = height();
      c.setColor(kWell);
      c.roundedRectangle(0, 0, w, h, 4 * s);
      c.setColor(kRule);
      c.roundedRectangleBorder(0, 0, w, h, 4 * s, 1.0f);
      const auto fx = [&](double f) { return static_cast<float>(std::log10(f / 20.0) / 3.0) * w; };
      const auto dbY = [&](double d) { return h / 2 - static_cast<float>(d / 18.0) * (h / 2 - 4 * s); };
      c.setColor(0xff262b33);
      for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
         c.fill(fx(f), 1, 1.0f, h - 2);
      c.setColor(0xff22262d);
      for (double d : {-12.0, -6.0, 6.0, 12.0})
         c.fill(1, dbY(d), w - 2, 1.0f);
      c.setColor(0xff343a44);
      c.fill(1, dbY(0), w - 2, 1.0f);
      if (labels) {
         c.setColor(kFaint);
         for (const auto &[f, l] : {std::pair<double, const char *>{100.0, "100"}, {1000.0, "1k"}, {10000.0, "10k"}})
            c.text(l, ui::font(7.5f * s), visage::Font::kLeft, fx(f) + 3 * s, h - 13 * s, 30 * s, 10 * s);
      }
      const Bands b = bands ? bands() : Bands{};
      const int n = std::max(40, static_cast<int>(w / (2 * s)));
      float px = 0, py = dbY(response(b, 20.0));
      for (int i = 1; i <= n; ++i) {
         const double f = 20.0 * std::pow(10.0, 3.0 * i / n);
         const float x = static_cast<float>(i) / n * w, y = dbY(response(b, f));
         // The area between the curve and 0 dB, faint.
         c.setColor(alpha(colour, 0.13f));
         const float y0 = dbY(0);
         c.fill(px, std::min(y, y0), x - px + 0.5f, std::fabs(y - y0));
         c.setColor(colour);
         c.segment(px, py, x, y, 1.6f * s, true);
         px = x;
         py = y;
      }
      const auto dot = [&](double f) {
         if (f <= 0)
            return;
         const float x = fx(f), y = dbY(response(b, f));
         c.setColor(0xff15181d);
         c.circle(x - 3.2f * s, y - 3.2f * s, 6.4f * s);
         c.setColor(colour);
         c.ring(x - 3.2f * s, y - 3.2f * s, 6.4f * s, 1.4f * s);
      };
      dot(b.lowCut);
      dot(120.0);
      dot(b.midHz);
      dot(7000.0);
      dot(b.highCut);
   }
};

// OUTPUT's picture: every pad where its pan puts it (higher for brighter roles), glowing while it
// sounds, between the width brackets; and the KIT, FX and OUT level bars.
class OutputView : public visage::Frame {
public:
   explicit OutputView(EditorHost &host) : host_(host) {}
   float fieldWidth = 153.0f;
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float fw = fieldWidth * s, fh = height() - 16 * s;
      c.setColor(0x59000000);
      c.roundedRectangle(0, 0, fw, fh, 5 * s);
      c.setColor(0x1fffffff);
      c.fill(6 * s, fh / 2, fw - 12 * s, 1.0f);
      c.fill(fw / 2, 5 * s, 1.0f, fh - 10 * s);
      const double width = host_.paramModulated(kWidth);
      const auto xOf = [&](double p) { return fw / 2 + static_cast<float>(p) * (fw / 2 - 10 * s); };
      const double shown = std::min(1.0, width);
      c.setColor(0x59ffffff);
      for (int sd : {-1, 1})
         c.fill(xOf(sd * shown) - 1 * s, 6 * s, 2 * s, fh - 12 * s);
      static const float roleY[kRoleCount] = {0.85f, 0.5f, 0.2f, 0.45f, 0.65f, 0.55f, 0.3f};
      for (int i = 0; i < kPads; ++i) {
         const SamplePtr p = host_.padSample(i);
         if (!p)
            continue;
         const double pan = host_.paramModulated(padParam(i, kPan)) * std::min(1.0, width);
         // Spread pads of a role a little so they don't sit on each other.
         const float jitter = ((i * 37) % 11 - 5) * 0.012f;
         const float cx = xOf(pan), cy = 7 * s + (roleY[std::clamp(p->role, 0, kRoleCount - 1)] + jitter) * (fh - 14 * s);
         const float lv = host_.padLevel(i);
         const unsigned col = roleColour(p->role);
         if (lv > 0.01f) {
            c.setColor(ui::steadyGlow(alpha(col, std::min(1.0f, 0.3f + lv)), {cx, cy}, 10 * s));
            c.circle(cx - 10 * s, cy - 10 * s, 20 * s);
         }
         const double db = host_.paramModulated(padParam(i, kLevel));
         const float r = (lv > 0.01f ? 3.4f : 2.2f) * s * static_cast<float>(0.7 + 0.5 * std::clamp((db + 24) / 36, 0.0, 1.0));
         c.setColor(alpha(col, lv > 0.01f ? 1.0f : 0.55f));
         c.circle(cx - r, cy - r, 2 * r);
      }
      c.setColor(0x80ffffff);
      c.text("L", ui::font(8.5f * s), visage::Font::kLeft, 2 * s, fh + 3 * s, 20 * s, 12 * s);
      c.text("WIDTH", ui::font(8 * s), visage::Font::kCenter, 0, fh + 3 * s, fw, 12 * s);
      c.text("R", ui::font(8.5f * s), visage::Font::kRight, fw - 22 * s, fh + 3 * s, 20 * s, 12 * s);

      // Level bars, each with a tick where its setting is.
      const float mx = fw + 13 * s;
      const float set[3] = {static_cast<float>((host_.paramValue(kVolume) + 60) / 66),
                            static_cast<float>((std::max(host_.paramValue(kReverbReturn), host_.paramValue(kDelayReturn)) + 60) / 66),
                            static_cast<float>((host_.paramValue(kVolume) + 60) / 66)};
      const unsigned colours[3] = {kOrange, kPurple, 0xffdfe6f5};
      const char *names[3] = {"KIT", "FX", "OUT"};
      for (int i = 0; i < 3; ++i) {
         const float x = mx + i * 16 * s, bw = 8 * s;
         const float v = std::clamp(shown_[i], 0.0f, 1.0f);
         c.setColor(0x12ffffff);
         c.roundedRectangle(x, 0, bw, fh, 3 * s);
         if (v > 0.002f) {
            c.setColor(ui::steady(visage::Color(colours[i]).withAlpha(0.35f)));
            c.roundedRectangle(x - 2 * s, fh - v * fh - 2 * s, bw + 4 * s, v * fh + 2 * s, 4 * s);
            c.setColor(colours[i]);
            c.roundedRectangle(x, fh - v * fh, bw, v * fh, 3 * s);
         }
         c.setColor(0xe6ffffff);
         c.fill(x - 3 * s, fh - std::clamp(set[i], 0.0f, 1.0f) * fh - 1 * s, bw + 6 * s, 2 * s);
         c.setColor(0x8cffffff);
         c.text(names[i], ui::font(7 * s), visage::Font::kCenter, x - 4 * s, fh + 3 * s, bw + 8 * s, 12 * s);
      }
   }
   // Peaks to dB-ish bar heights, falling slowly.
   void update(float kit, float fx, float out) {
      const float in[3] = {kit, fx, out};
      for (int i = 0; i < 3; ++i) {
         const float db = in[i] > 1e-5f ? 20.0f * std::log10(in[i]) : -100.0f;
         const float v = std::clamp((db + 54.0f) / 60.0f, 0.0f, 1.0f);
         shown_[i] = std::max(v, shown_[i] - 0.025f);
      }
      redraw();
   }
   float shown_[3] = {0, 0, 0};

private:
   EditorHost &host_;
};

// ---- the panel ----

std::vector<double> defaultValues() {
   std::vector<double> d(kParamCount);
   for (clap_id i = 0; i < kParamCount; ++i)
      d[i] = param(i).defaultValue;
   return d;
}

std::filesystem::path soundsFile(const std::filesystem::path &preset) {
   std::filesystem::path p = preset;
   p.replace_extension(kKitSoundsExtension);
   return p;
}

const std::vector<ui::Preset> &noFactoryKits() {
   static const std::vector<ui::Preset> none;
   return none;
}

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   enum Page { kGeneratePage, kKitPage };

   explicit Panel(EditorHost &host) : DesignFrame(kDesignWidth, kDesignHeight), host_(host), bindings_(host) {
      tips_.maxWidth = 260.0f;
      tips_.add(add<ui::HoverArea>({14, 12, 130, 40}), "JKit version " JCLAP_PLUGIN_VERSION);
      tabs_ = &add<ui::TabBar>({150, 18, 220, 28}, std::vector<std::string>{"GENERATE", "KIT"});
      tabs_->setAccent(kOrange);
      tabs_->onChange = [this](int p) {
         host_.page = p;
         redraw();
      };
      add<ui::UpdateNotice>({382, 21, 140, 22});
      buildPresets();
      buildGenerate();
      buildKit();
      tabs_->select(host_.page);
      refresh();
      startTimer(33);
   }

   void showPage(int page) {
      tabs_->select(page);
      host_.page = page;
   }

   // ---- the timer ----

   void timerCallback() override {
      std::string error;
      if (host_.pollGeneration(error)) {
         lastError_.clear();
         selectVariant(host_.selectedVariant, true);
      } else if (!error.empty())
         lastError_ = error;
      refresh();
   }

   void refresh() {
      if (host_.page == kGeneratePage)
         refreshGenerate();
      else
         refreshKit();
   }

   void draw(visage::Canvas &c) override {
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      c.setColor(t.background);
      c.fill(0, 0, width(), height());
      c.setColor(t.headerBar);
      c.fill(0, 0, width(), kHeaderHeight * s);
      // The logo: four pads, two lit.
      c.setColor(kOrange);
      c.roundedRectangleBorder(19 * s, 17 * s, 30 * s, 30 * s, 7 * s, 2 * s);
      c.roundedRectangle(25 * s, 23 * s, 7.5f * s, 7.5f * s, 1.5f * s);
      c.roundedRectangle(35.5f * s, 33.5f * s, 7.5f * s, 7.5f * s, 1.5f * s);
      c.roundedRectangleBorder(35.5f * s, 23 * s, 7.5f * s, 7.5f * s, 1.5f * s, 1.4f * s);
      c.roundedRectangleBorder(25 * s, 33.5f * s, 7.5f * s, 7.5f * s, 1.5f * s, 1.4f * s);
      c.text("J", ui::font(28 * s), visage::Font::kLeft, 59 * s, 12 * s, 30 * s, 40 * s);
      c.setColor(t.text);
      c.text("Kit", ui::font(28 * s), visage::Font::kLeft, 72 * s, 12 * s, 80 * s, 40 * s);
      if (host_.page == kGeneratePage)
         drawGenerate(c, s);
      else
         drawKit(c, s);
   }

private:
   template <typename T, typename... Args>
   T &onPage(Page page, ui::Rect bounds, Args &&...args) {
      return tabs_->addToPage(page, add<T>(bounds, std::forward<Args>(args)...));
   }

   // ---- the kit browser ----

   void buildPresets() {
      presets_ = &add<ui::PresetBar>({726, 18, 336, 28}, noFactoryKits(), kKitFormat, host_);
      presets_->setAccent(kOrange);
      presets_->defaults = defaultValues();
      presets_->currentName = [this] { return host_.kitName(); };
      presets_->setCurrentName = [this](const std::string &n) { host_.setKitName(n); };
      presets_->onLoad = [this](const ui::Preset &p) {
         ui::applyPreset(p, host_, defaultValues());
         const std::filesystem::path file = ui::userPresetFolder(kKitFormat) / (p.name + kKitFormat.extension);
         std::ifstream in(soundsFile(file), std::ios::binary);
         std::stringstream bytes;
         bytes << in.rdbuf();
         SamplePtr pads[kPads];
         if (readKit(bytes.str(), pads))
            for (int i = 0; i < kPads; ++i)
               host_.setPadSample(i, pads[i]);
      };
      presets_->onSaved = [this](const std::filesystem::path &file) {
         SamplePtr pads[kPads];
         for (int i = 0; i < kPads; ++i)
            pads[i] = host_.padSample(i);
         std::ofstream out(soundsFile(file), std::ios::binary);
         const std::string bytes = writeKit(pads);
         out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      };
      presets_->onDeleted = [](const std::filesystem::path &file) {
         std::error_code ec;
         std::filesystem::remove(soundsFile(file), ec);
      };
      tips_.add(*presets_, "Your kits: step through them, open the list, or SAVE this one with its sounds.");
   }

   // ---- Generate ----

   void buildGenerate() {
      const Page P = kGeneratePage;
      // MODEL
      engine_ = &onPage<ui::Dropdown>(P, {24, 128, 188, 22},
                                      std::vector<std::string>{"Stable Audio Open Small", "ORB", "Stable Audio Open 1.0"});
      engine_->setAccent(kOrange);
      engine_->onChange = [this](int i) {
         GenSettings &g = host_.gen();
         if (g.engine != i) {
            // Each Stable Audio wants its own steps and guidance: start from what it's tuned for.
            if (i == 0) {
               g.steps = 8;
               g.guidance = 1.0;
            } else if (i == 2) {
               g.steps = 50;
               g.guidance = 7.0;
            }
         }
         g.engine = i;
         steps_->setValue(g.steps);
         guidance_->setValue(g.guidance);
         confirmRemove_ = false;
         lastError_.clear();
         syncEngine();
         redraw();
      };
      tips_.add(*engine_, "The engine that makes the sounds, inside JKit on your CPU. Stable Audio Open Small: "
                          "neural and quick, 2.2 GB. ORB: plans the sound and plays it with physical models, 20.8 MB. "
                          "Stable Audio Open 1.0: the full model, slower and richer, 4.8 GB.");
      engineAction_ = &onPage<ui::Button>(P, {310, 128, 62, 22}, "DOWNLOAD");
      engineAction_->setAccent(kOrange);
      engineAction_->onClick = [this] { engineAction(); };
      tips_.add(*engineAction_, "Download this engine, stop a download, or remove it to free its disk space. "
                                "Each engine downloads and is removed on its own.");

      // PROMPT
      prompt_ = &onPage<PromptBox>(P, {24, 220, 348, 62});
      prompt_->onChange = [this](const std::string &t) { host_.gen().prompt = t; };
      roles_ = &onPage<ChipRow>(P, {24, 306, 316, 20});
      roles_->onChange = [this](int r) { host_.gen().role = r; };
      tips_.add(*roles_, "What the sound is for: the random prompt, the pad's colour and its name.");
      auto &dice = onPage<IconButton>(P, {346, 306, 26, 20}, IconButton::Icon::Dice);
      dice.onClick = [this] { randomPrompt(); };
      tips_.add(dice, "A random prompt for this role. The slider below says how strange.");
      strange_ = &onPage<EndsSlider>(P, {24, 332, 348, 16}, "GENERIC", "STRANGE");
      strange_->onChange = [this](double v) { host_.gen().strange = v; };
      tips_.add(*strange_, "How strange a random prompt gets: studio drums at the left, objects and places at the right.");

      const float kw = 348.0f / 4, ky = 360;
      length_ = &genKnob({24, ky, kw, 80}, "LENGTH", ui::ValueRange{0.1, 11.8, 1.0, 0.1},
                         [](double v) { return secondsText(v); }, [this](double v) { host_.gen().seconds = v; });
      tips_.add(*length_, "How long the sound is, up to 11.8 s. Trim it after.");
      steps_ = &genKnob({24 + kw, ky, kw, 80}, "STEPS", ui::ValueRange{4, 16, 8, 1, false, true},
                        [](double v) { return std::to_string(static_cast<int>(std::lround(v))); },
                        [this](double v) { host_.gen().steps = static_cast<int>(std::lround(v)); });
      tips_.add(*steps_, "Diffusion steps. Small is tuned for 8, 1.0 for about 50; more is slower.");
      guidance_ = &genKnob({24 + 2 * kw, ky, kw, 80}, "GUIDANCE", ui::ValueRange{1, 7, 1, 0.1},
                           [](double v) {
                              char b[16];
                              std::snprintf(b, sizeof(b), "%.1f", v);
                              return std::string(b);
                           },
                           [this](double v) { host_.gen().guidance = v; });
      tips_.add(*guidance_, "How hard it follows the words. Small is tuned for 1, 1.0 for 7; above 1 takes twice as long.");
      variety_ = &genKnob({24 + 3 * kw, ky, kw, 80}, "VARIETY", ui::ValueRange{0, 1, 1, 0.01},
                          [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + "%"; },
                          [this](double v) { host_.gen().variety = v; });
      tips_.add(*variety_, "How different the variants are from each other: low makes takes of one sound.");

      const float sy = 446;
      seed_ = &onPage<ui::NumberBox>(P, {24, sy + 16, 116, 22}, ui::ValueRange{1, 99999, 1, 1, false, true});
      seed_->setStepButtons(true);
      seed_->setAccent(kOrange);
      seed_->onChange = [this](double v) { host_.gen().seed = static_cast<uint32_t>(std::lround(v)); };
      tips_.add(*seed_, "Where the randomness starts. The same seed and prompt make the same sound.");
      auto &seedDice = onPage<IconButton>(P, {144, sy + 16, 26, 22}, IconButton::Icon::Dice);
      seedDice.onClick = [this] {
         host_.gen().seed = 1 + std::random_device{}() % 99999;
         seed_->setValue(host_.gen().seed);
      };
      tips_.add(seedDice, "A new seed.");
      lock_ = &onPage<IconButton>(P, {174, sy + 16, 26, 22}, IconButton::Icon::Lock);
      lock_->onClick = [this] {
         host_.gen().seedLocked = !host_.gen().seedLocked;
         lock_->lit = host_.gen().seedLocked;
         lock_->redraw();
      };
      tips_.add(*lock_, "Keep this seed. Unlocked, each GENERATE picks a new one.");
      count_ = &onPage<ui::SegmentedSwitch>(P, {216, sy + 16, 156, 22}, std::vector<std::string>{"1", "2", "4", "8"});
      count_->setAccent(kOrange);
      count_->onChange = [this](int i) { host_.gen().variants = 1 << i; };
      tips_.add(*count_, "How many sounds each GENERATE makes, from neighbouring seeds.");

      generate_ = &onPage<GoButton>(P, {24, 496, 348, 40}, "GENERATE");
      generate_->onClick = [this] { generate(-1); };

      recentArea_ = &onPage<ui::HoverArea>(P, {24, 588, 348, 56});
      recentArea_->onMouseDown() += [this](const visage::MouseEvent &e) {
         const int i = static_cast<int>(e.position.y / (18 * scale()));
         const auto &r = host_.recent();
         if (i >= 0 && i < static_cast<int>(r.size()) && i < 3) {
            host_.gen().prompt = r[static_cast<size_t>(i)].prompt;
            host_.gen().role = r[static_cast<size_t>(i)].role;
            host_.gen().seed = r[static_cast<size_t>(i)].seed;
            syncGenerate();
         }
      };
      tips_.add(*recentArea_, "Your last prompts. Click one to load it, with its role and seed.");

      // VARIANTS
      variantsView_ = &onPage<VariantGrid>(P, {404, 114, 652, 186}, host_);
      variantsView_->onSelect = [this](int i) { selectVariant(i, true); };

      // SELECTED
      trim_ = &onPage<TrimView>(P, {404, 358, 652, 96}, host_);
      trim_->onClick = [this](double) { auditionSelected(); };
      tips_.add(*trim_, "Drag the white handles to trim, and pull the top of the end down into a fade. Click to hear it.");
      normalize_ = &onPage<CheckBox>(P, {404, 480, 90, 16}, "NORMALIZE");
      normalize_->onToggle = [this](bool on) { host_.treatment().normalize = on; };
      trimSilence_ = &onPage<CheckBox>(P, {494, 480, 102, 16}, "TRIM SILENCE");
      trimSilence_->onToggle = [this](bool on) { host_.treatment().trimSilence = on; };
      reverse_ = &onPage<CheckBox>(P, {596, 480, 90, 16}, "REVERSE");
      reverse_->onToggle = [this](bool on) { host_.treatment().reverse = on; };
      tips_.add(*normalize_, "Bring its peak up to just under full scale.");
      tips_.add(*trimSilence_, "Cut the silence before it starts and after it fades out.");
      vary_ = &onPage<ui::Button>(P, {884, 475, 84, 22}, "VARY");
      vary_->onClick = [this] { generate(host_.selectedVariant); };
      tips_.add(*vary_, "More sounds like this one: same prompt, noise close to its seed.");
      auditionButton_ = &onPage<ui::Button>(P, {972, 475, 84, 22}, "AUDITION");
      auditionButton_->onClick = [this] { auditionSelected(); };
      tips_.add(*auditionButton_, "Hear it as it will go on the pad: trimmed, faded and normalized.");

      picker_ = &onPage<PadPicker>(P, {404, 528, 260, 88}, host_);
      picker_->onPick = [this](int) { redraw(); };
      tips_.add(*picker_, "The pad it goes on. Pad 1 is bottom left, on MIDI note 36.");
      addToKit_ = &onPage<GoButton>(P, {674, 594, 382, 30}, "ADD TO KIT");
      addToKit_->onClick = [this] { addToKit(); };
   }

   ui::Knob &genKnob(ui::Rect r, const char *label, ui::ValueRange range, std::function<std::string(double)> textOf,
                     std::function<void(double)> onChange) {
      auto &k = onPage<ui::Knob>(kGeneratePage, r, range);
      k.setLabel(label);
      k.setAccent(kOrange);
      k.valueText = std::move(textOf);
      k.onChange = std::move(onChange);
      return k;
   }

   void randomPrompt() {
      GenSettings &g = host_.gen();
      g.prompt = jkit::randomPrompt(static_cast<Role>(g.role), g.strange, std::random_device{}());
      prompt_->setText(g.prompt);
   }

   void generate(int around) {
      const EngineStatus e = host_.engine();
      if (host_.generating())
         return;
      if (e.state == EngineStatus::State::Missing || e.state == EngineStatus::State::Failed)
         host_.installEngine();
      host_.generate(around);
      variantsView_->auditioning = nullptr;
   }

   // The selected variant, treated as it will go on a pad.
   SamplePtr treated() const {
      const auto &v = host_.variants();
      const int i = host_.selectedVariant;
      if (i < 0 || i >= static_cast<int>(v.size()) || !v[static_cast<size_t>(i)].sample)
         return nullptr;
      const SamplePtr &src = v[static_cast<size_t>(i)].sample;
      auto out = std::make_shared<Sample>(treat(*src, host_.treatment()));
      return out;
   }

   void selectVariant(int i, bool play) {
      const auto &v = host_.variants();
      if (i < 0 || i >= static_cast<int>(v.size()))
         return;
      host_.selectedVariant = i;
      // A new sound starts untrimmed.
      Treatment &t = host_.treatment();
      if (trim_->sample != v[static_cast<size_t>(i)].sample) {
         t.start = 0.0;
         t.end = 1.0;
         t.fadeMs = 0.0;
      }
      trim_->sample = v[static_cast<size_t>(i)].sample;
      if (play) {
         host_.audition(v[static_cast<size_t>(i)].sample);
         variantsView_->auditioning = v[static_cast<size_t>(i)].sample.get();
      }
      redraw();
   }

   void auditionSelected() {
      if (SamplePtr t = treated()) {
         host_.audition(t);
         auditioned_ = t;
         variantsView_->auditioning = nullptr;
      }
   }

   void addToKit() {
      SamplePtr t = treated();
      if (!t)
         return;
      auto s = std::make_shared<Sample>(*t);
      s->name = uniqueName(kRoleNames[std::clamp(s->role, 0, kRoleCount - 1)], host_.targetPad);
      const int pad = host_.targetPad;
      host_.setPadSample(pad, s);
      // A new sound plays from its start to its end.
      for (PadParam f : {kStart, kEnd}) {
         const clap_id id = padParam(pad, f);
         host_.beginEdit(id);
         host_.setParam(id, f == kStart ? 0.0 : 1.0);
         host_.endEdit(id);
      }
      host_.selectedPad = pad;
      added_ = pad;
      addedAt_ = 0;
      // The next empty pad is the next target.
      for (int k = 1; k < kPads; ++k) {
         const int p = (pad + k) % kPads;
         if (!host_.padSample(p)) {
            host_.targetPad = p;
            break;
         }
      }
      redraw();
   }

   // "Kick", or "Kick 2" when another pad has that name.
   std::string uniqueName(const std::string &base, int pad) const {
      for (int n = 1;; ++n) {
         const std::string name = n == 1 ? base : base + " " + std::to_string(n);
         bool taken = false;
         for (int i = 0; i < kPads; ++i)
            if (i != pad)
               if (SamplePtr p = host_.padSample(i); p && p->name == name)
                  taken = true;
         if (!taken)
            return name;
      }
   }

   void engineAction() {
      const EngineStatus e = host_.engine();
      switch (e.state) {
      case EngineStatus::State::Missing:
      case EngineStatus::State::Failed:
         if (e.diskBytes > 0 && e.state == EngineStatus::State::Failed && confirmRemove_) {
            host_.removeEngine();
            confirmRemove_ = false;
         } else
            host_.installEngine();
         break;
      case EngineStatus::State::Downloading:
         host_.cancelEngine();
         break;
      default:
         // Removing takes two clicks: the first asks.
         if (confirmRemove_) {
            host_.removeEngine();
            confirmRemove_ = false;
         } else
            confirmRemove_ = true;
         break;
      }
   }

   bool orb() const { return host_.gen().engine == 1; }

   // ORB plans the sound and plays it: it has no diffusion steps, guidance or variety.
   void syncEngine() {
      engine_->setSelected(host_.gen().engine);
      const bool open1 = host_.gen().engine == 2;
      steps_->setRange(open1 ? ui::ValueRange{10, 100, 50, 1, false, true} : ui::ValueRange{4, 16, 8, 1, false, true});
      guidance_->setRange(open1 ? ui::ValueRange{1, 12, 7, 0.1} : ui::ValueRange{1, 7, 1, 0.1});
      steps_->setValue(host_.gen().steps);
      guidance_->setValue(host_.gen().guidance);
      steps_->setVisible(!orb());
      guidance_->setVisible(!orb());
      variety_->setVisible(!orb());
   }

   void syncGenerate() {
      const GenSettings &g = host_.gen();
      syncEngine();
      prompt_->setText(g.prompt);
      roles_->selected = g.role;
      roles_->redraw();
      strange_->setValue(g.strange);
      length_->setValue(g.seconds);
      steps_->setValue(g.steps);
      guidance_->setValue(g.guidance);
      variety_->setValue(g.variety);
      seed_->setValue(g.seed);
      lock_->lit = g.seedLocked;
      lock_->redraw();
      int idx = 0;
      while ((1 << idx) < g.variants && idx < 3)
         ++idx;
      count_->setSelected(idx);
      const Treatment &t = host_.treatment();
      normalize_->setOn(t.normalize);
      trimSilence_->setOn(t.trimSilence);
      reverse_->setOn(t.reverse);
   }

   void refreshGenerate() {
      if (!synced_) {
         syncGenerate();
         synced_ = true;
         const auto &v = host_.variants();
         if (host_.selectedVariant >= 0 && host_.selectedVariant < static_cast<int>(v.size()))
            trim_->sample = v[static_cast<size_t>(host_.selectedVariant)].sample;
      }
            status_ = host_.engine();
      const bool busy = host_.generating();
      const double p = busy ? host_.generateProgress() : -3.0;
      if (busy)
         generate_->setLabel(p < 0 ? (status_.state == EngineStatus::State::Downloading ? "WAITING FOR THE DOWNLOAD"
                                                                                         : "LOADING THE MODEL")
                                   : "GENERATING " + std::to_string(static_cast<int>(p * 100)) + "%");
      else
         generate_->setLabel(host_.gen().variants > 1 ? "GENERATE " + std::to_string(host_.gen().variants)
                                                       : std::string("GENERATE"));
      generate_->setProgress(busy ? std::max(-1.0, p) : -3.0);
      // The engine's button.
      std::string action;
      switch (status_.state) {
      case EngineStatus::State::Missing:
         action = "DOWNLOAD";
         break;
      case EngineStatus::State::Downloading:
         action = "CANCEL";
         break;
      case EngineStatus::State::Failed:
         action = "RETRY";
         break;
      default:
         action = confirmRemove_ ? "SURE?" : "REMOVE";
         break;
      }
      engineAction_->setText(action);
      const bool haveSound = trim_->sample != nullptr;
      addToKit_->setEnabled(haveSound);
      vary_->setVisible(true);
      const float pos = host_.auditionPosition();
      const auto &vars = host_.variants();
      trim_->playhead = (pos >= 0 && auditioned_ == nullptr && variantsView_->auditioning == trim_->sample.get()) ? pos : -1;
      if (pos < 0)
         auditioned_ = nullptr;
      (void)vars;
      trim_->redraw();
      variantsView_->emptyText = host_.generating() ? "Generating..." : "Variants appear here";
      variantsView_->redraw();
      picker_->redraw();
      redraw();
   }

   void drawGenerate(visage::Canvas &c, float s) {
      const ui::Theme &t = ui::defaultTheme();
      const EngineStatus &e = status_;
      // MODEL
      std::string note = orb() ? "ORB by Marcos Maceo \xC2\xB7 Apache-2.0" : "Powered by Stability AI";
      if (e.state == EngineStatus::State::Downloading)
         note = std::to_string(e.total ? static_cast<int>(100.0 * e.received / e.total) : 0) + "% \xC2\xB7 " +
                bytesText(e.received) + " of " + bytesText(e.total);
      drawCard(c, s, 12, 76, 372, 98, "MODEL", note);
      drawLabel(c, s, "ENGINE", 24, 112);
      // The state: a dot and a word.
      unsigned dot = kFaint;
      std::string state;
      switch (e.state) {
      case EngineStatus::State::Missing:
         state = "Not downloaded";
         break;
      case EngineStatus::State::Downloading: {
         state = "Downloading";
         dot = kOrange;
         break;
      }
      case EngineStatus::State::Installed:
         state = "Downloaded";
         dot = kBlue;
         break;
      case EngineStatus::State::Loading:
         state = "Loading...";
         dot = kYellow;
         break;
      case EngineStatus::State::Ready:
         state = "Loaded \xC2\xB7 CPU";
         dot = kGreen;
         break;
      case EngineStatus::State::Failed:
         state = "Failed";
         dot = 0xffe0676b;
         break;
      }
      c.setColor(dot);
      c.circle(219 * s, 136 * s, 7 * s);
      c.setColor(e.state == EngineStatus::State::Downloading ? kOrange : t.text);
      c.text(state, ui::font(10 * s), visage::Font::kLeft, 231 * s, 128 * s, 78 * s, 22 * s);
      // The sizes, always: what it downloads and what it takes now.
      std::string sizes;
      if (e.state == EngineStatus::State::Downloading) {
         c.setColor(t.track);
         c.roundedRectangle(24 * s, 154 * s, 348 * s, 4 * s, 2 * s);
         c.setColor(kOrange);
         c.roundedRectangle(24 * s, 154 * s, 348 * s * (e.total ? static_cast<float>(e.received) / e.total : 0.0f),
                            4 * s, 2 * s);
         sizes = "";
      } else if (e.state == EngineStatus::State::Failed && !e.error.empty()) {
         sizes = ellipsis(e.error, 70);
      } else if (e.diskBytes > 0) {
         sizes = bytesText(e.diskBytes) + " on disk \xC2\xB7 " + (orb() ? "27 one-shot models" : "up to 11.8 s, 44.1 kHz stereo") +
                 " \xC2\xB7 REMOVE frees it";
      } else {
         sizes = bytesText(e.downloadBytes) + " download \xC2\xB7 nothing on disk yet \xC2\xB7 " +
                 (orb() ? "27 one-shot models" : "up to 11.8 s, 44.1 kHz");
      }
      if (!lastError_.empty() && e.state != EngineStatus::State::Downloading)
         sizes = ellipsis(lastError_, 70);
      c.setColor(alpha(t.dimText, 0.75f));
      c.text(sizes, ui::font(8.5f * s), visage::Font::kLeft, 24 * s, 156 * s, 348 * s, 12 * s);

      // PROMPT
      drawCard(c, s, 12, 182, 372, 466, "PROMPT", "what the sound should be");
      drawLabel(c, s, "ROLE", 24, 290);
      c.setColor(kRule);
      c.fill(24 * s, 354 * s, 348 * s, 1.0f * s);
      if (orb()) {
         c.setColor(t.dimText);
         const float x = 24 + 348.0f / 4 + 8;
         c.text("ORB plans the sound from your words,", ui::font(9 * s), visage::Font::kLeft, x * s, 378 * s, 250 * s, 14 * s);
         c.text("then plays it with physical models.", ui::font(9 * s), visage::Font::kLeft, x * s, 392 * s, 250 * s, 14 * s);
         c.text("Variants play the same plan, new seeds.", ui::font(9 * s), visage::Font::kLeft, x * s, 412 * s, 250 * s, 14 * s);
      }
      drawLabel(c, s, "SEED", 24, 446);
      drawLabel(c, s, "VARIANTS", 216, 446);
      std::string hint;
      if (host_.generating())
         hint = "about " + std::to_string(std::max(1, static_cast<int>(std::lround(estimate())))) + " s on this computer";
      else if (host_.lastRunSeconds() > 0)
         hint = "last run: " + std::to_string(host_.variants().size()) + " sounds in " + secondsText(host_.lastRunSeconds());
      else if (e.state == EngineStatus::State::Missing)
         hint = "The first GENERATE downloads the model (" + bytesText(e.downloadBytes) + ")";
      else
         hint = "Each sound is new: same prompt and seed, same sound";
      c.setColor(t.dimText);
      c.text(hint, ui::font(9 * s), visage::Font::kCenter, 24 * s, 542 * s, 348 * s, 14 * s);
      c.setColor(kRule);
      c.fill(24 * s, 566 * s, 348 * s, 1.0f * s);
      drawLabel(c, s, "RECENT", 24, 572);
      const auto &recent = host_.recent();
      if (recent.empty()) {
         c.setColor(kFaint);
         c.text("Your prompts will be kept here", ui::font(9.5f * s), visage::Font::kLeft, 24 * s, 592 * s, 348 * s,
                14 * s);
      }
      for (size_t i = 0; i < recent.size() && i < 3; ++i) {
         const float y = 588 + 18.0f * i;
         c.setColor(roleColour(recent[i].role));
         c.circle(26 * s, (y + 5) * s, 5 * s);
         c.setColor(kTitle);
         c.text(ellipsis(recent[i].prompt, 46), ui::font(10 * s), visage::Font::kLeft, 38 * s, y * s, 280 * s, 15 * s);
         c.setColor(t.dimText);
         c.text(agoText(recent[i].when), ui::font(9 * s), visage::Font::kRight, 312 * s, y * s, 60 * s, 15 * s);
      }

      // VARIANTS
      const auto &vars = host_.variants();
      std::string seeds;
      if (!vars.empty())
         seeds = vars.size() == 1 ? "seed " + std::to_string(vars[0].sample->seed)
                                  : std::to_string(vars.size()) + " sounds";
      drawCard(c, s, 392, 76, 676, 236, "VARIANTS", seeds);

      // SELECTED
      const int sel = host_.selectedVariant;
      std::string title = "SELECTED", info;
      if (trim_->sample && sel >= 0 && sel < static_cast<int>(vars.size())) {
         title += " \xC2\xB7 " + std::string(1, static_cast<char>('A' + sel));
         info = "seed " + std::to_string(trim_->sample->seed) + " \xC2\xB7 " + secondsText(trim_->sample->seconds());
      }
      drawCard(c, s, 392, 320, 676, 328, title, info);
      if (trim_->sample) {
         const Treatment &tr = host_.treatment();
         const double secs = trim_->sample->seconds();
         c.setColor(t.dimText);
         c.text("START " + secondsText(tr.start * secs), ui::font(8.5f * s), visage::Font::kLeft, 406 * s, 458 * s,
                120 * s, 12 * s);
         c.text("END " + secondsText(tr.end * secs), ui::font(8.5f * s), visage::Font::kRight, 936 * s, 458 * s,
                120 * s, 12 * s);
         c.setColor(kBlue);
         c.text(tr.fadeMs > 0.5 ? "FADE " + std::to_string(static_cast<int>(tr.fadeMs)) + " ms" : "pull the top for a fade",
                ui::font(8.5f * s), visage::Font::kCenter, 404 * s, 458 * s, 652 * s, 12 * s);
      }
      c.setColor(kRule);
      c.fill(404 * s, 504 * s, 652 * s, 1.0f * s);
      drawLabel(c, s, "SEND TO PAD", 404, 510);
      const int pad = host_.targetPad;
      const SamplePtr p = host_.padSample(pad);
      std::string padTitle = "Pad " + std::to_string(pad + 1) + (p ? " \xC2\xB7 " + p->name : " \xC2\xB7 empty");
      c.setColor(t.text);
      c.text(padTitle, ui::font(11 * s), visage::Font::kLeft, 674 * s, 528 * s, 380 * s, 14 * s);
      c.setColor(alpha(t.dimText, 0.75f));
      c.text(p ? "replaces its sound; the prompt and seed go with it"
               : "on MIDI note " + std::to_string(kFirstNote + pad) + " (" + ui::noteName(kFirstNote + pad) + ")",
             ui::font(8.5f * s), visage::Font::kLeft, 674 * s, 544 * s, 380 * s, 12 * s);
      if (added_ >= 0 && addedAt_ < 60) {
         ++addedAt_;
         c.setColor(kGreen);
         c.text("Added to pad " + std::to_string(added_ + 1), ui::font(9 * s), visage::Font::kLeft, 674 * s,
                566 * s, 380 * s, 12 * s);
      }
   }

   // A rough guess at a generation's time from the last one.
   double estimate() const {
      const double last = host_.lastRunSeconds();
      const GenSettings &g = host_.gen();
      if (last > 0)
         return last;
      if (orb())
         return 0.5 + 0.4 * g.variants;
      if (g.engine == 2) // 1.0: about 0.3 s a step on 4 cores for a short sound, twice with guidance
         return 5.0 + 0.3 * g.steps * (g.guidance > 1.001 ? 2 : 1) * std::max(1.0, g.seconds / 1.5) * g.variants;
      return 2.0 + 2.5 * g.variants;
   }

   // ---- Kit ----

   void buildKit() {
      const Page P = kKitPage;
      pads_ = &onPage<PadGrid>(P, {24, 114, 336, 394}, host_);
      pads_->onSelect = [this](int i) {
         host_.selectedPad = i;
         syncPad();
         redraw();
      };
      pads_->onEmpty = [this](int i) {
         host_.targetPad = i;
         showPage(kGeneratePage);
      };
      pads_->onMenu = [this](int i, visage::Point) {
         visage::PopupMenu menu;
         const bool has = host_.padSample(i) != nullptr;
         menu.addOption(1, "Play");
         menu.addOption(2, "Regenerate in the Generate tab");
         menu.addBreak();
         menu.addOption(3, "Clear pad");
         menu.onSelection() = [this, i](int id) {
            if (id == 1)
               host_.playPad(i, 0.9f);
            else if (id == 2)
               regenerate(i);
            else if (id == 3)
               host_.setPadSample(i, nullptr);
            redraw();
         };
         (void)has;
         menu.show(pads_);
      };
      tips_.add(*pads_, "Click a pad to play and select it; drag it onto another to swap them. Right-click for more.");

      regen_ = &onPage<ui::Button>(P, {906, 81, 150, 19}, "REGENERATE IN TAB");
      regen_->setAccent(kOrange);
      regen_->onClick = [this] { regenerate(host_.selectedPad); };
      tips_.add(*regen_, "Open the Generate tab with this pad's prompt, role and seed, ready to make another take.");
      wave_ = &onPage<PadWave>(P, {392, 128, 214, 78}, host_);
      tips_.add(*wave_, "Drag the handles to set where it starts and ends. The blue curve is its decay.");

      choke_ = &onPage<ui::Dropdown>(P, {392, 226, 66, 20}, std::vector<std::string>(kChokeNames, kChokeNames + kChokeCount));
      choke_->setAccent(kOrange);
      choke_->onChange = [this](int i) { setPad(kChoke, i); };
      tips_.add(*choke_, "Pads in the same group cut each other off, like a closed hat stopping an open one.");
      play_ = &onPage<ui::SegmentedSwitch>(P, {466, 226, 92, 20}, std::vector<std::string>{"ONE SHOT", "GATE"});
      play_->setAccent(kOrange);
      play_->onChange = [this](int i) { setPad(kPlayMode, i); };
      tips_.add(*play_, "One shot plays the whole sound; Gate stops it when the note ends.");
      rev_ = &onPage<CheckBox>(P, {566, 228, 48, 16}, "REV");
      rev_->onToggle = [this](bool on) { setPad(kReverse, on ? 1 : 0); };
      tips_.add(*rev_, "Play it backwards.");

      const float kx = 614, kw = (1056.0f - kx) / 4;
      const PadParam top[4] = {kLevel, kPan, kPitch, kDecay}, bottom[4] = {kStart, kEnd, kAttack, kVelocity};
      const char *topNames[4] = {"LEVEL", "PAN", "PITCH", "DECAY"}, *bottomNames[4] = {"START", "END", "ATTACK", "VELOCITY"};
      for (int i = 0; i < 4; ++i) {
         padKnob(top[i], topNames[i], {kx + i * kw, 108, kw, 70}, kOrange);
         padKnob(bottom[i], bottomNames[i], {kx + i * kw, 176, kw, 70}, kOrange);
      }
      // PAD EQ
      eq_ = &onPage<EqView>(P, {392, 298, 536, 126});
      eq_->bands = [this] {
         const int p = host_.selectedPad;
         EqView::Bands b;
         const double cut = host_.paramModulated(padParam(p, kLowCut));
         b.lowCut = lowCutOff(cut) ? 0 : lowCutHz(cut);
         b.low = host_.paramModulated(padParam(p, kLow));
         b.mid = host_.paramModulated(padParam(p, kMid));
         b.midHz = midFreqHz(host_.paramModulated(padParam(p, kMidFreq)));
         b.high = host_.paramModulated(padParam(p, kHigh));
         return b;
      };
      const PadParam eqs[5] = {kLowCut, kLow, kMid, kMidFreq, kHigh};
      const char *eqNames[5] = {"LOW CUT", "LOW", "MID", "MID FREQ", "HIGH"};
      for (int i = 0; i < 5; ++i)
         padKnob(eqs[i], eqNames[i], {392 + i * 536.0f / 5, 430, 536.0f / 5, 66}, kOrange);
      // SENDS
      padKnob(kReverbSend, "REVERB", {960, 296, 96, 76}, kPurple);
      padKnob(kDelaySend, "DELAY", {960, 418, 96, 76}, kBlue);

      // The master band.
      const float ky = 566, kh = 72, kwm = 48;
      masterEq_ = &onPage<EqView>(P, {24, 570, 100, 68});
      masterEq_->colour = kWhite;
      masterEq_->labels = false;
      masterEq_->bands = [this] {
         EqView::Bands b;
         b.lowCut = 30;
         b.highCut = 18000;
         b.low = host_.paramModulated(kMasterLow);
         b.mid = host_.paramModulated(kMasterMid);
         b.midHz = midFreqHz(host_.paramModulated(kMasterMidFreq));
         b.high = host_.paramModulated(kMasterHigh);
         return b;
      };
      const clap_id meq[4] = {kMasterLow, kMasterMid, kMasterMidFreq, kMasterHigh};
      const char *meqNames[4] = {"LOW", "MID", "MID FREQ", "HIGH"};
      for (int i = 0; i < 4; ++i)
         masterKnob(meq[i], meqNames[i], {134 + i * kwm, ky, kwm, kh}, kWhite);
      // Reverb and delay, one card.
      auto &revType = onPage<ui::Dropdown>(P, {442, 538, 60, 18},
                                           std::vector<std::string>(kReverbTypeNames, kReverbTypeNames + kReverbTypeCount));
      revType.setAccent(kPurple);
      bindings_.bind(revType, kReverbType);
      tips_.add(revType, "The reverb's character: Room, Hall, Plate (bright) or Dark.");
      const clap_id rev[3] = {kReverbSize, kReverbDecay, kReverbReturn};
      const char *revNames[3] = {"SIZE", "DECAY", "RETURN"};
      for (int i = 0; i < 3; ++i)
         masterKnob(rev[i], revNames[i], {358 + i * kwm, ky, kwm, kh}, kPurple);
      std::vector<std::string> sync;
      for (int i = 0; i < kDelaySyncChoices; ++i)
         sync.emplace_back(delaySyncNames()[i]);
      auto &dlySync = onPage<ui::Dropdown>(P, {611, 538, 60, 18}, sync);
      dlySync.setAccent(kBlue);
      bindings_.bind(dlySync, kDelaySync);
      tips_.add(dlySync, "The delay's time, as a note value at the host's tempo.");
      const clap_id dly[3] = {kDelayFeedback, kDelayTone, kDelayReturn};
      const char *dlyNames[3] = {"FEEDBACK", "TONE", "RETURN"};
      for (int i = 0; i < 3; ++i)
         masterKnob(dly[i], dlyNames[i], {527 + i * kwm, ky, kwm, kh}, kBlue);
      // OUTPUT
      output_ = &onPage<OutputView>(P, {691, 568, 211, 74}, host_);
      tips_.add(*output_, "Each pad where its pan puts it, lit while it plays; then the dry kit, the effects and the output.");
      masterKnob(kWidth, "WIDTH", {912, ky, kwm, kh}, kWhite);
      auto &boom = masterKnob(kBoom, "BOOM", {912 + kwm, ky, kwm, kh}, kOrange);
      tips_.add(boom, "Low-end punch: the lows under 110 Hz, driven and added back.");
      masterKnob(kVolume, "VOLUME", {912 + 2 * kwm, ky, kwm, kh}, kWhite);
   }

   void regenerate(int pad) {
      SamplePtr p = host_.padSample(pad);
      host_.targetPad = pad;
      if (p) {
         GenSettings &g = host_.gen();
         g.prompt = p->prompt;
         g.role = p->role;
         g.seed = p->seed;
         synced_ = false;
      }
      showPage(kGeneratePage);
   }

   void setPad(PadParam f, double v) {
      const clap_id id = padParam(host_.selectedPad, f);
      host_.beginEdit(id);
      host_.setParam(id, v);
      host_.endEdit(id);
   }

   // A knob for the selected pad: it follows the selection, so it isn't bound to one param.
   ui::Knob &padKnob(PadParam f, const char *label, ui::Rect r, unsigned colour) {
      const clap_id first = padParam(0, f);
      const auto &d = param(first);
      const bool stepped = (d.flags & CLAP_PARAM_IS_STEPPED) != 0;
      const bool bipolar = f == kPan || f == kPitch || f == kLow || f == kMid || f == kHigh;
      auto &k = onPage<ui::Knob>(kKitPage, r,
                                 ui::ValueRange{d.min, d.max, d.defaultValue, f == kPitch ? 0.1 : 0.0, bipolar, stepped});
      k.setLabel(label);
      k.setAccent(colour);
      k.valueText = [this, f](double v) { return padText(f, v); };
      k.parseText = [f](const std::string &t, double &v) { return parseParam(padParam(0, f), t.c_str(), v); };
      k.onBeginEdit = [this, f] { host_.beginEdit(padParam(host_.selectedPad, f)); };
      k.onChange = [this, f](double v) {
         host_.setParam(padParam(host_.selectedPad, f), v);
         redraw();
      };
      k.onEndEdit = [this, f] { host_.endEdit(padParam(host_.selectedPad, f)); };
      k.modulatedSource = [this, f] { return host_.paramModulated(padParam(host_.selectedPad, f)); };
      padKnobs_.push_back({&k, f});
      return k;
   }

   // Start and End read as times into the selected pad's sound; the rest as the host shows them.
   std::string padText(PadParam f, double v) const {
      if (f == kStart || f == kEnd) {
         if (SamplePtr p = host_.padSample(host_.selectedPad)) {
            const double ms = v * p->seconds() * 1000.0;
            char b[24];
            if (ms >= 1000.0)
               std::snprintf(b, sizeof(b), "%.2f s", ms / 1000.0);
            else
               std::snprintf(b, sizeof(b), "%.0f ms", ms);
            return b;
         }
      }
      return text(padParam(0, f), v);
   }

   ui::Knob &masterKnob(clap_id id, const char *label, ui::Rect r, unsigned colour) {
      const auto &d = param(id);
      const bool bipolar = id == kMasterLow || id == kMasterMid || id == kMasterHigh;
      auto &k = onPage<ui::Knob>(kKitPage, r, ui::ValueRange{d.min, d.max, d.defaultValue, 0.0, bipolar, false});
      k.setLabel(label);
      k.setAccent(colour);
      k.valueText = [id](double v) { return text(id, v); };
      k.parseText = [id](const std::string &t, double &v) { return parseParam(id, t.c_str(), v); };
      bindings_.bind(k, id);
      return k;
   }

   void syncPad() {
      const int p = host_.selectedPad;
      for (auto &[k, f] : padKnobs_) {
         const clap_id id = padParam(p, f);
         k->setValue(host_.paramValue(id), host_.paramModulated(id));
      }
      choke_->setSelected(static_cast<int>(host_.paramValue(padParam(p, kChoke)) + 0.5));
      play_->setSelected(static_cast<int>(host_.paramValue(padParam(p, kPlayMode)) + 0.5));
      rev_->setOn(host_.paramValue(padParam(p, kReverse)) >= 0.5);
   }

   void refreshKit() {
      syncPad();
      float kit, fx, out;
      host_.meters(kit, fx, out);
      output_->update(kit, fx, out);
      pads_->redraw();
      wave_->redraw();
      eq_->redraw();
      masterEq_->redraw();
      redraw();
   }

   void drawKit(visage::Canvas &c, float s) {
      const ui::Theme &t = ui::defaultTheme();
      drawCard(c, s, 12, 76, 360, 432, "PADS", "drag one onto another to swap");
      const int pad = host_.selectedPad;
      const SamplePtr p = host_.padSample(pad);
      std::string title = "PAD " + std::to_string(pad + 1);
      if (p) {
         std::string n = p->name;
         for (char &ch : n)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
         title += " \xC2\xB7 " + n;
      }
      drawCard(c, s, 380, 76, 688, 176, title);
      c.setColor(alpha(t.dimText, 0.75f));
      const std::string line = p ? "seed " + std::to_string(p->seed) + " \xC2\xB7 \xE2\x80\x9C" + ellipsis(p->prompt, 34) +
                                      "\xE2\x80\x9D"
                                 : "note " + ui::noteName(kFirstNote + pad) + " \xC2\xB7 nothing on it yet";
      c.text(line, ui::font(8.5f * s), visage::Font::kLeft, 392 * s, 110 * s, 214 * s, 12 * s);
      drawLabel(c, s, "CHOKE", 392, 212);
      drawLabel(c, s, "PLAY", 466, 212);
      drawCard(c, s, 380, 260, 560, 248, "PAD EQ");
      drawCard(c, s, 948, 260, 120, 248, "SENDS");

      // The master band: everything under it acts on the whole kit.
      c.setColor(t.headerBar);
      c.fill(0, kMasterTop * s, width(), height() - kMasterTop * s);
      c.setColor(t.panelBorder);
      c.fill(0, kMasterTop * s, width(), 1.0f);
      drawCard(c, s, 12, 532, 326, 116, "MASTER EQ", "cuts 30 Hz \xC2\xB7 18 kHz");
      // Reverb and delay share a card, each half with its own coloured title.
      drawCard(c, s, 346, 532, 325, 116, "");
      c.setColor(t.panel);
      c.fill(358 * s, 559 * s, 302 * s, 3 * s);
      for (int h = 0; h < 2; ++h) {
         const float hx = h ? 527 : 358;
         c.setColor(h ? kBlue : kPurple);
         c.text(h ? "DELAY" : "REVERB", ui::font(11 * s), visage::Font::kLeft, hx * s, 540 * s, 80 * s, 16 * s);
         c.setColor(kRule);
         c.fill(hx * s, 560 * s, 144 * s, 1.0f * s);
      }
      c.setColor(kRule);
      c.fill(508 * s, 542 * s, 1.0f * s, 96 * s);
      drawCard(c, s, 679, 532, 389, 116, "OUTPUT");
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   ui::TabBar *tabs_ = nullptr;
   ui::PresetBar *presets_ = nullptr;

   // Generate
   ui::Dropdown *engine_ = nullptr;
   ui::Button *engineAction_ = nullptr;
   PromptBox *prompt_ = nullptr;
   ChipRow *roles_ = nullptr;
   EndsSlider *strange_ = nullptr;
   ui::Knob *length_ = nullptr, *steps_ = nullptr, *guidance_ = nullptr, *variety_ = nullptr;
   ui::NumberBox *seed_ = nullptr;
   IconButton *lock_ = nullptr;
   ui::SegmentedSwitch *count_ = nullptr;
   GoButton *generate_ = nullptr, *addToKit_ = nullptr;
   ui::HoverArea *recentArea_ = nullptr;
   VariantGrid *variantsView_ = nullptr;
   TrimView *trim_ = nullptr;
   CheckBox *normalize_ = nullptr, *trimSilence_ = nullptr, *reverse_ = nullptr;
   ui::Button *vary_ = nullptr, *auditionButton_ = nullptr;
   PadPicker *picker_ = nullptr;
   EngineStatus status_;
   std::string lastError_;
   SamplePtr auditioned_;
   bool synced_ = false, confirmRemove_ = false;
   int added_ = -1, addedAt_ = 0;

   // Kit
   PadGrid *pads_ = nullptr;
   ui::Button *regen_ = nullptr;
   PadWave *wave_ = nullptr;
   ui::Dropdown *choke_ = nullptr;
   ui::SegmentedSwitch *play_ = nullptr;
   CheckBox *rev_ = nullptr;
   EqView *eq_ = nullptr, *masterEq_ = nullptr;
   OutputView *output_ = nullptr;
   std::vector<std::pair<ui::Knob *, PadParam>> padKnobs_;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host),
                                       ui::WindowOptions{static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight)});
}

bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.stopTimer();
   panel.showPage(page);
   panel.setBounds(0, 0, static_cast<float>(width), static_cast<float>(height));
   panel.timerCallback();
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jkit
