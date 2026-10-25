#include "jclap/ui/value_control.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <visage/widgets.h>

#include "jclap/core/helpers.h"
#include "jclap/ui/design_frame.h"

namespace jclap::ui {

std::string ValueAxis::text(double v) const {
   if (valueText)
      return valueText(v);
   char buffer[32];
   std::snprintf(buffer, sizeof(buffer), range.step >= 1.0 ? "%.0f" : "%.2f", v);
   return buffer;
}

ValuePopup::ValuePopup() { setIgnoresMouseEvents(true, false); }

void ValuePopup::show(visage::Frame &target, const std::string &text) {
   visage::Frame *top = target.topParentFrame();
   if (!top || top == &target)
      return;
   if (host_ != top) {
      hide();
      host_ = top;
      top->addChild(this);
      setOnTop(true);
   }
   text_ = text;
   const visage::Bounds b = top->relativeBounds(&target);
   const float s = designScale(target);
   const float h = std::clamp(b.height() * 0.22f, 18.0f * s, 28.0f * s);
   const float w = std::max(h * 2.5f, font(h * 0.6f, top->dpiScale()).stringWidth(visage::String(text_).toUtf32()) + h);
   setBounds(b.xCenter() - w * 0.5f, std::max(0.0f, b.y() - h - 4.0f), w, h);
   redraw();
}

void ValuePopup::hide() {
   if (host_)
      host_->removeChild(this);
   host_ = nullptr;
}

void ValuePopup::draw(visage::Canvas &canvas) {
   const Theme &t = *theme_;
   const float r = height() * 0.3f;
   canvas.setColor(t.headerBar);
   canvas.roundedRectangle(0, 0, width(), height(), r);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width(), height(), r, 1.0f);
   canvas.setColor(t.text);
   canvas.text(text_, font(height() * 0.6f), visage::Font::kCenter, 0, 0, width(), height());
}

ValueControl::ValueControl(ValueRange range) : range_(range) {
   value_ = modulated_ = range_.clamp(range_.defaultValue);
}

ValueControl::~ValueControl() {
   // Destroyed mid-drag (the editor closed): close the gesture, or the host keeps the param touched.
   if (dragging_ && onEndEdit)
      onEndEdit();
   popup_.hide();
}

void ValueControl::setRange(const ValueRange &range) {
   range_ = range;
   value_ = range_.clamp(value_);
   modulated_ = range_.clamp(modulated_);
   redraw();
}

void ValueControl::setValue(double value, double modulated) {
   if (dragging_)
      value = value_;
   if (value != value_ || modulated != modulated_) {
      value_ = value;
      modulated_ = modulated;
      redraw();
   }
}

void ValueControl::setLabel(std::string label) {
   label_ = std::move(label);
   redraw();
}

void ValueControl::setCaption(std::string caption) {
   caption_ = std::move(caption);
   redraw();
}

void ValueControl::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void ValueControl::setTheme(const Theme &theme) {
   theme_ = &theme;
   popup_.setTheme(theme);
   redraw();
}

std::string ValueControl::formatValue(double v) const {
   if (valueText)
      return valueText(v);
   if (range_.step >= 1.0)
      return std::to_string(std::lround(v));
   char buffer[32];
   std::snprintf(buffer, sizeof(buffer), "%.2f", v);
   return buffer;
}

void ValueControl::change(double value) {
   value_ = range_.snap(value);
   if (onChange)
      onChange(value_);
   modulated_ = modulatedSource ? modulatedSource() : value_;
   redraw();
}

void ValueControl::userSet(double value) {
   if (onBeginEdit)
      onBeginEdit();
   change(value);
   if (onEndEdit)
      onEndEdit();
}

void ValueControl::mouseDown(const visage::MouseEvent &e) {
   // Another button pressed mid-drag belongs to the drag, which the first release ends.
   if (dragging_)
      return;
   if (e.shouldTriggerPopup()) {
      showContextMenu(e);
      return;
   }
   if (e.repeatClickCount() == 2) {
      dragging_ = false;
      resetToDefault();
      return;
   }
   dragging_ = true;
   dragStart_ = e.position;
   dragStartValue_ = value_;
   dragFine_ = e.isShiftDown();
   if (onBeginEdit)
      onBeginEdit();
   if (showPopup_)
      popup_.show(*this, text());
}

void ValueControl::mouseDrag(const visage::MouseEvent &e) {
   if (!dragging_)
      return;
   // Hold shift for fine control. Pressing or letting go of it mid-drag carries on from here,
   // instead of rescaling the whole move so far.
   if (e.isShiftDown() != dragFine_) {
      dragFine_ = e.isShiftDown();
      dragStart_ = e.position;
      dragStartValue_ = value_;
   }
   const float length = dragLength() * (dragFine_ ? 10.0f : 1.0f);
   if (range_.stops.size() >= 2) {
      // Evenly through the stops, however far apart their values are.
      const double last = static_cast<double>(range_.stops.size() - 1);
      const double at = range_.stopPosition(dragStartValue_) + dragDistance(dragStart_, e.position) * last / length;
      const size_t i = static_cast<size_t>(std::clamp(std::round(at), 0.0, last));
      if (range_.stops[i] != value_)
         change(range_.stops[i]);
   }
   else
      change(dragStartValue_ + dragDistance(dragStart_, e.position) * (range_.max - range_.min) / length);
   if (showPopup_)
      popup_.show(*this, text());
}

void ValueControl::mouseUp(const visage::MouseEvent &) {
   if (dragging_ && onEndEdit)
      onEndEdit();
   dragging_ = false;
   popup_.hide();
}

bool ValueControl::mouseWheel(const visage::MouseEvent &e) {
   const float delta = e.precise_wheel_delta_y != 0.0f ? e.precise_wheel_delta_y : e.wheel_delta_y;
   if (delta == 0.0f || !(range_.max > range_.min))
      return false;
   // A wheel step mid-drag would nest a second gesture inside the drag's, and the next drag event
   // would undo it anyway.
   if (dragging_)
      return true;
   userSet(range_.stepFrom(value_, delta > 0 ? 1 : -1));
   return true;
}

void ValueControl::showContextMenu(const visage::MouseEvent &e) {
   visage::PopupMenu menu;
   menu.addOption(0, "Reset to default").onSelection() += [this](int) { resetToDefault(); };
   menu.addOption(1, "Enter value...").onSelection() += [this](int) { startTextEntry(); };
   if (onContextMenu)
      onContextMenu(menu);
   menu.show(this, e.position);
}

void ValueControl::startTextEntry() {
   if (!editor_) {
      editor_ = std::make_unique<visage::TextEditor>();
      editor_->setNumberEntry();
      editor_->setJustification(visage::Font::kCenter);
      editor_->onEnterKey() += [this] { finishTextEntry(true); };
      editor_->onEscapeKey() += [this] { finishTextEntry(false); };
      // Clicking elsewhere takes the value, as Enter would.
      editor_->onFocusChange() += [this](bool focused, bool) {
         if (!focused)
            finishTextEntry(true);
      };
      addChild(editor_.get(), false);
   }
   const float s = designScale(*this);
   const float h = std::clamp(height() * 0.22f, 16.0f * s, 30.0f * s);
   editor_->setBounds(0, (height() - h) * 0.5f, width(), h);
   editor_->setFont(font(h * 0.65f));
   editor_->setText(formatValue(value_));
   editor_->setVisible(true);
   editor_->requestKeyboardFocus();
   editor_->selectAll();
}

void ValueControl::finishTextEntry(bool apply) {
   if (!editor_ || !editor_->isVisible())
      return;
   const std::string typed = editor_->text().toUtf8();
   editor_->setVisible(false);
   if (!apply)
      return;
   double v = 0.0;
   bool ok;
   if (parseText)
      ok = parseText(typed, v);
   else
      ok = core::parseNumber(typed.c_str(), v); // a '.' decimal point in every locale
   if (ok && std::isfinite(v))
      userSet(v);
}

} // namespace jclap::ui
