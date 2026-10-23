#pragma once

// Shared behaviour of Knob, Slider and XYPad's axes: a value in [min, max] that the user drags,
// scrolls or double-clicks back to its default, plus an optional modulated value the host is
// actually using. Right-click opens a menu with "Reset to default" and "Enter value...".

#include <functional>
#include <memory>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/theme.h"

namespace visage {
class TextEditor;
}

namespace jclap::ui {

struct ValueRange {
   double min = 0.0;
   double max = 1.0;
   double defaultValue = 0.0;
   // Mouse wheel step; 0 means 1/100 of the range.
   double step = 0.0;
   // Draw the value from the middle of the range instead of from min.
   bool bipolar = false;

   double clamp(double v) const { return v < min ? min : v > max ? max : v; }
   // Position of a value in the range, 0 to 1.
   float fraction(double v) const { return max > min ? static_cast<float>((v - min) / (max - min)) : 0.0f; }
};

// A small rounded box that floats above a control and shows its value while it is dragged.
class ValuePopup : public visage::Frame {
public:
   ValuePopup();
   void setTheme(const Theme &theme) { theme_ = &theme; }
   // Shows `text` centred above `target`, in the top-level frame's coordinates.
   void show(visage::Frame &target, const std::string &text);
   void hide();
   void draw(visage::Canvas &canvas) override;

private:
   const Theme *theme_ = &defaultTheme();
   visage::Frame *host_ = nullptr;
   std::string text_;
};

class ValueControl : public visage::Frame {
public:
   explicit ValueControl(ValueRange range = {});
   ~ValueControl() override;

   const ValueRange &range() const { return range_; }
   void setRange(const ValueRange &range);

   double value() const { return value_; }
   double modulatedValue() const { return modulated_; }

   // Updates from the host (automation, modulation, state load). Doesn't fire the callbacks, and
   // leaves the value alone while the user is dragging.
   void setValue(double value) { setValue(value, value); }
   void setValue(double value, double modulated);

   void setLabel(std::string label);
   const std::string &label() const { return label_; }
   // Small dimmed text under the value, e.g. "CC 74".
   void setCaption(std::string caption);
   void setAccent(unsigned color);
   void setTheme(const Theme &theme);
   // Float the value above the control while it is dragged. Off by default.
   void setShowValuePopup(bool show) { showPopup_ = show; }

   // How the value is shown. Defaults to a whole number when step >= 1, else two decimals.
   std::function<std::string(double)> valueText;
   // Reads a typed value. Defaults to the leading number in the text.
   std::function<bool(const std::string &, double &)> parseText;

   // A gesture is onBeginEdit, any number of onChange, then onEndEdit.
   std::function<void()> onBeginEdit;
   std::function<void(double)> onChange;
   std::function<void()> onEndEdit;
   // Called after the user changes the value, to fetch the modulated value to show. Optional.
   std::function<double()> modulatedSource;
   // Adds items to the right-click menu, after the built-in ones (e.g. the host's param menu).
   std::function<void(visage::PopupMenu &)> onContextMenu;

   // Sets the value as one whole gesture, as if the user did it.
   void userSet(double value);
   void resetToDefault() { userSet(range_.defaultValue); }
   // Opens a text box over the control to type a value into.
   void startTextEntry();
   std::string text() const { return formatValue(modulated_); }
   std::string formatValue(double v) const;

   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

protected:
   float fraction(double v) const { return range_.fraction(v); }
   // How far the mouse has moved along the control's axis (up/right is positive), and how many
   // points that takes to cover the whole range.
   virtual float dragDistance(visage::Point from, visage::Point to) const = 0;
   virtual float dragLength() const = 0;
   unsigned accent() const { return accent_ ? accent_ : theme_->accent; }

   const Theme *theme_ = &defaultTheme();
   std::string label_, caption_;

private:
   void change(double value);
   void showContextMenu(const visage::MouseEvent &e);
   void finishTextEntry(bool apply);

   ValueRange range_;
   double value_ = 0.0, modulated_ = 0.0;
   unsigned accent_ = 0;
   bool dragging_ = false;
   bool showPopup_ = false;
   visage::Point dragStart_;
   double dragStartValue_ = 0.0;
   ValuePopup popup_;
   std::unique_ptr<visage::TextEditor> editor_;
};

} // namespace jclap::ui
