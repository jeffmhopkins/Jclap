#include "jclap/ui/update_notice.h"

#include <algorithm>
#include <optional>

#include "jclap/ui/design_frame.h"

namespace jclap::ui {
namespace {

// How often to look for a check's result. It only reads memory.
constexpr int kPollMs = 1000;

} // namespace

UpdateNotice::UpdateNotice() {
   setVisible(false);
   startTimer(kPollMs);
}

void UpdateNotice::show(const updates::Release &release) {
   if (release.version == release_.version && release.url == release_.url)
      return;
   const bool was = !release_.version.empty();
   release_ = release;
   const bool now = !release_.version.empty();
   setVisible(now);
   redraw();
   if (was != now && parent())
      parent()->redraw();
}

void UpdateNotice::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

void UpdateNotice::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

UpdateNotice::Layout UpdateNotice::layout() const {
   Layout l;
   const float w = width(), h = height(), pad = h * 0.5f;
   l.textSize = h * 0.46f;
   // The longest wording that fits beside the cross, and the pill only as wide as it needs.
   const visage::Font measure = font(l.textSize, dpiScale());
   const std::string v = "v" + release_.version;
   float textWidth = 0.0f;
   for (const std::string &option : {"Update available: " + v, "Update: " + v, v}) {
      l.label = option;
      textWidth = measure.stringWidth(visage::String(option).toUtf32());
      if (textWidth <= w - h - pad)
         break;
   }
   l.width = std::min(w, pad + textWidth + h);
   l.x = alignRight ? w - l.width : 0.0f;
   return l;
}

void UpdateNotice::draw(visage::Canvas &canvas) {
   if (release_.version.empty())
      return;
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const Layout l = layout();
   const float h = height(), r = h * 0.5f, pad = h * 0.5f;
   canvas.setColor(hover_ && !hoverClose_ ? t.buttonHover : t.button);
   canvas.roundedRectangle(l.x, 0, l.width, h, r);
   canvas.setColor(accent);
   canvas.roundedRectangleBorder(l.x, 0, l.width, h, r, 1.0f);
   canvas.text(l.label, font(l.textSize), visage::Font::kLeft, l.x + pad, 0, l.width - h - pad, h);

   // The cross that dismisses it.
   const float cx = l.x + l.width - h * 0.5f, cy = h * 0.5f, arm = h * 0.16f;
   canvas.setColor(hoverClose_ ? t.text : t.dimText);
   canvas.segment(cx - arm, cy - arm, cx + arm, cy + arm, h * 0.07f, true);
   canvas.segment(cx - arm, cy + arm, cx + arm, cy - arm, h * 0.07f, true);
}

bool UpdateNotice::overPill(visage::Point p) const {
   const Layout l = layout();
   return p.x >= l.x && p.x <= l.x + l.width;
}

bool UpdateNotice::overClose(visage::Point p) const {
   const Layout l = layout();
   return p.x >= l.x + l.width - height() && p.x <= l.x + l.width;
}

void UpdateNotice::mouseEnter(const visage::MouseEvent &e) { mouseMove(e); }

void UpdateNotice::mouseExit(const visage::MouseEvent &) {
   hover_ = hoverClose_ = false;
   redraw();
}

void UpdateNotice::mouseMove(const visage::MouseEvent &e) {
   const bool over = overPill(e.position), close = overClose(e.position);
   if (over != hover_ || close != hoverClose_) {
      hover_ = over;
      hoverClose_ = close;
      redraw();
   }
}

void UpdateNotice::mouseDown(const visage::MouseEvent &e) {
   if (release_.version.empty() || !overPill(e.position))
      return;
   if (e.shouldTriggerPopup()) {
      for (visage::Frame *f = parent(); f; f = f->parent()) {
         if (auto *design = dynamic_cast<DesignFrame *>(f)) {
            design->showZoomMenu(*this, e.position);
            return;
         }
      }
      return;
   }
   if (overClose(e.position)) {
      updates::dismiss(release_.version);
      show({});
   }
   else {
      updates::openInBrowser(release_.url);
   }
}

void UpdateNotice::timerCallback() { show(updates::available().value_or(updates::Release{})); }

} // namespace jclap::ui
