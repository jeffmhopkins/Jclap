#pragma once

// A panel laid out on a fixed design grid (say 1160 x 732) and scaled to whatever size the window
// is. Children are placed in design units and re-laid out on every resize.
//
//    class MyPanel : public jclap::ui::DesignFrame {
//    public:
//       MyPanel() : DesignFrame(600, 400) {
//          add<Section>({10, 10, 580, 380}, "FILTER");
//          for (Rect cell : row({20, 40, 560, 100}, 4))
//             add<Knob>(cell, range);
//       }
//    };

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <visage/ui.h>

namespace jclap::ui {

struct Rect {
   float x = 0, y = 0, w = 0, h = 0;

   Rect translated(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
   Rect reduced(float margin) const { return {x + margin, y + margin, w - 2 * margin, h - 2 * margin}; }
};

// Splits `area` into equal cells: left to right, top to bottom, or a grid in reading order.
std::vector<Rect> row(Rect area, int count, float gap = 0.0f);
std::vector<Rect> column(Rect area, int count, float gap = 0.0f);
std::vector<Rect> grid(Rect area, int columns, int rows, float gap = 0.0f);

// A widget whose sizes don't follow from its bounds (e.g. a Section's title font). DesignFrame calls
// setScale() with the design-to-pixel ratio whenever it lays out.
class Scalable {
public:
   virtual ~Scalable() = default;
   virtual void setScale(float scale) = 0;
};

class DesignFrame;
// The scale of the DesignFrame `frame` is in (1 outside one), for sizes that should follow the UI
// zoom but aren't laid out by it, such as popups and drag distances.
float designScale(const visage::Frame &frame);

class DesignFrame : public visage::Frame {
public:
   DesignFrame(float designWidth, float designHeight);

   float designWidth() const { return designWidth_; }
   float designHeight() const { return designHeight_; }
   // Pixels per design unit: the largest that fits both ways, so nothing is cut off when the frame
   // has another aspect ratio (Window centres the frame and keeps its shape).
   float scale() const { return std::min(width() / designWidth_, height() / designHeight_); }

   // Creates a child at `bounds` (design units). The panel owns it; children draw in the order added.
   template <typename T, typename... Args>
   T &add(Rect bounds, Args &&...args) {
      auto frame = std::make_unique<T>(std::forward<Args>(args)...);
      T &ref = *frame;
      placed_.push_back({&ref, bounds});
      addChild(std::move(frame));
      layout(placed_.back());
      return ref;
   }

   // Places a child the caller owns (e.g. a member).
   void place(visage::Frame &frame, Rect bounds);
   // Moves a child already added or placed.
   void move(visage::Frame &frame, Rect bounds);

   // When set, right-clicking the background offers these UI sizes (1.0 = design size), above
   // "Check for Updates", which it always offers.
   std::function<void(float)> onZoom;
   std::vector<float> zoomLevels = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
   // The size shown as ticked. Window keeps it in step with the size the window really has.
   float currentZoom = 1.0f;

   void resized() override;
   void mouseDown(const visage::MouseEvent &e) override;
   // Shows the background menu at `position` in `source`: the UI sizes (if onZoom is set) and
   // "Check for Updates", which turns the release check on or off for every JClap plugin. For a
   // child that covers the background, so right-clicking it offers the same.
   void showZoomMenu(visage::Frame &source, visage::Point position);

private:
   struct Placed {
      visage::Frame *frame;
      Rect bounds;
   };

   void layout(const Placed &p);

   float designWidth_, designHeight_;
   std::vector<Placed> placed_;
};

} // namespace jclap::ui
