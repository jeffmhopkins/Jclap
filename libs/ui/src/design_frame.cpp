#include "jclap/ui/design_frame.h"

#include <cmath>
#include <string>

#include <visage/ui.h>

#include "jclap/ui/updates.h"

namespace jclap::ui {

std::vector<Rect> row(Rect area, int count, float gap) {
   std::vector<Rect> cells;
   const float w = (area.w - gap * (count - 1)) / count;
   for (int i = 0; i < count; ++i)
      cells.push_back({area.x + i * (w + gap), area.y, w, area.h});
   return cells;
}

std::vector<Rect> column(Rect area, int count, float gap) {
   std::vector<Rect> cells;
   const float h = (area.h - gap * (count - 1)) / count;
   for (int i = 0; i < count; ++i)
      cells.push_back({area.x, area.y + i * (h + gap), area.w, h});
   return cells;
}

std::vector<Rect> grid(Rect area, int columns, int rows, float gap) {
   std::vector<Rect> cells;
   for (const Rect &r : column(area, rows, gap)) {
      for (const Rect &c : row(r, columns, gap))
         cells.push_back(c);
   }
   return cells;
}

float designScale(const visage::Frame &frame) {
   for (const visage::Frame *f = &frame; f; f = f->parent()) {
      if (auto *design = dynamic_cast<const DesignFrame *>(f))
         return design->scale() > 0.0f ? design->scale() : 1.0f;
   }
   return 1.0f;
}

DesignFrame::DesignFrame(float designWidth, float designHeight)
   : designWidth_(designWidth), designHeight_(designHeight) {}

void DesignFrame::place(visage::Frame &frame, Rect bounds) {
   placed_.push_back({&frame, bounds});
   addChild(&frame);
   layout(placed_.back());
}

void DesignFrame::move(visage::Frame &frame, Rect bounds) {
   for (Placed &p : placed_) {
      if (p.frame == &frame) {
         p.bounds = bounds;
         layout(p);
      }
   }
}

void DesignFrame::layout(const Placed &p) {
   const float s = scale();
   p.frame->setBounds(p.bounds.x * s, p.bounds.y * s, p.bounds.w * s, p.bounds.h * s);
   if (auto *scalable = dynamic_cast<Scalable *>(p.frame))
      scalable->setScale(s);
}

void DesignFrame::resized() {
   for (const Placed &p : placed_)
      layout(p);
}

void DesignFrame::mouseDown(const visage::MouseEvent &e) {
   if (e.shouldTriggerPopup())
      showZoomMenu(*this, e.position);
}

void DesignFrame::showZoomMenu(visage::Frame &source, visage::Point position) {
   constexpr int kCheckForUpdates = 1000;
   visage::PopupMenu menu;
   if (onZoom) {
      visage::PopupMenu sizes("UI Size");
      for (size_t i = 0; i < zoomLevels.size(); ++i) {
         const int percent = static_cast<int>(std::lround(zoomLevels[i] * 100.0f));
         sizes.addOption(static_cast<int>(i), std::to_string(percent) + "%")
            .select(std::fabs(zoomLevels[i] - currentZoom) < 0.005f);
      }
      menu.addSubMenu(sizes);
   }
   menu.addOption(kCheckForUpdates, "Check for Updates").select(updates::enabled());
   menu.onSelection() += [this](int id) {
      // The window resizes only if the host agrees, and sets currentZoom when it does.
      if (onZoom && id >= 0 && id < static_cast<int>(zoomLevels.size()))
         onZoom(zoomLevels[id]);
      else if (id == kCheckForUpdates)
         updates::setEnabled(!updates::enabled());
   };
   menu.show(&source, position);
}

} // namespace jclap::ui
