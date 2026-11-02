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

void Tooltips::setText(visage::Frame &frame, std::string text) {
   for (auto &[f, t] : tips_) {
      if (f == &frame)
         t = std::move(text);
   }
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
   // One line if it fits in maxWidth (or that's 0), otherwise wrapped at spaces (and at any newline).
   const visage::Bounds b = top->relativeBounds(&frame);
   const float line = 22.0f * designScale(frame);
   const visage::Font f = font(line * 0.55f, top->dpiScale());
   const auto width = [&](const std::string &s) { return f.stringWidth(visage::String(s).toUtf32()); };
   const float wrapAt = maxWidth > 0.0f ? maxWidth * designScale(frame) : 1e9f;
   popup_.lines.clear();
   std::string current;
   size_t i = 0;
   while (i <= text->size()) {
      const size_t end = std::min(text->find_first_of(" \n", i), text->size());
      const std::string word = text->substr(i, end - i);
      const std::string tried = current.empty() ? word : current + " " + word;
      if (!current.empty() && width(tried) > wrapAt) {
         popup_.lines.push_back(current);
         current = word;
      } else {
         current = tried;
      }
      if (end < text->size() && (*text)[end] == '\n') {
         popup_.lines.push_back(current);
         current.clear();
      }
      i = end + 1;
   }
   if (!current.empty())
      popup_.lines.push_back(current);
   float w = 0.0f;
   for (const auto &l : popup_.lines)
      w = std::max(w, width(l));
   w += line;
   popup_.lineHeight = line * 0.72f;
   const float h = line + (popup_.lines.size() > 1 ? (popup_.lines.size() - 1) * popup_.lineHeight : 0.0f);
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
   // A line is 22 design units tall (see show()); lineHeight is 0.72 of that.
   const float line = lineHeight / 0.72f;
   const visage::Font f = font(line * 0.55f);
   if (lines.size() <= 1) {
      canvas.text(lines.empty() ? std::string() : lines[0], f, visage::Font::kCenter, 0, 0, width() - 1, height() - 2);
      return;
   }
   const float pad = line * 0.5f;
   for (size_t i = 0; i < lines.size(); ++i)
      canvas.text(lines[i], f, visage::Font::kLeft, pad, i * lineHeight, width() - 1 - 2 * pad, line - 2);
}

} // namespace jclap::ui
