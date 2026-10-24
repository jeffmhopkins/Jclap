#include "jclap/ui/tooltip.h"

#include <algorithm>

#include "jclap/ui/design_frame.h"

namespace jclap::ui {

Tooltips::Tooltips() : self_(std::make_shared<Tooltips *>(this)) {}

Tooltips::~Tooltips() {
   // The frames' callbacks outlive this when the frames do; they check self_ and do nothing.
   *self_ = nullptr;
   hide();
}

void Tooltips::add(visage::Frame &frame, std::string text) {
   tips_.emplace_back(&frame, std::move(text));
   std::weak_ptr<Tooltips *> self = self_;
   frame.onMouseEnter() += [self, &frame](const visage::MouseEvent &) {
      if (auto s = self.lock(); s && *s) {
         (*s)->hide();
         (*s)->pending_ = &frame;
         (*s)->startTimer((*s)->delayMs);
      }
   };
   const auto hideTip = [self](const visage::MouseEvent &) {
      if (auto s = self.lock(); s && *s)
         (*s)->hide();
   };
   frame.onMouseExit() += hideTip;
   frame.onMouseDown() += hideTip;
}

void Tooltips::setTheme(const Theme &theme) { popup_.theme = &theme; }

const std::string *Tooltips::textFor(const visage::Frame *frame) const {
   for (const auto &[f, text] : tips_) {
      if (f == frame)
         return &text;
   }
   return nullptr;
}

void Tooltips::timerCallback() {
   stopTimer();
   if (pending_)
      show(*pending_);
}

void Tooltips::show(visage::Frame &frame) {
   const std::string *text = textFor(&frame);
   visage::Frame *top = frame.topParentFrame();
   if (!text || !top || top == &frame)
      return;
   if (host_ != top) {
      hide();
      host_ = top;
      top->addChild(&popup_);
      popup_.setOnTop(true);
   }
   popup_.text = *text;
   const visage::Bounds b = top->relativeBounds(&frame);
   const float h = 22.0f * designScale(frame);
   const float w = font(h * 0.55f, top->dpiScale()).stringWidth(visage::String(*text).toUtf32()) + h;
   // Under the widget, kept inside the window.
   float x = std::clamp(b.xCenter() - w * 0.5f, 2.0f, std::max(2.0f, top->width() - w - 2.0f));
   float y = b.bottom() + 4.0f;
   if (y + h > top->height())
      y = std::max(0.0f, b.y() - h - 4.0f);
   popup_.setBounds(x, y, w, h);
   popup_.redraw();
}

void Tooltips::hide() {
   stopTimer();
   pending_ = nullptr;
   if (host_)
      host_->removeChild(&popup_);
   host_ = nullptr;
}

void Tooltips::Popup::draw(visage::Canvas &canvas) {
   const Theme &t = *theme;
   const float r = height() * 0.25f;
   canvas.setColor(t.shadow);
   canvas.roundedRectangle(1, 2, width() - 1, height() - 2, r);
   canvas.setColor(t.headerBar);
   canvas.roundedRectangle(0, 0, width() - 1, height() - 2, r);
   canvas.setColor(t.panelBorder);
   canvas.roundedRectangleBorder(0, 0, width() - 1, height() - 2, r, 1.0f);
   canvas.setColor(t.text);
   canvas.text(text, font(height() * 0.55f), visage::Font::kCenter, 0, 0, width() - 1, height() - 2);
}

} // namespace jclap::ui
