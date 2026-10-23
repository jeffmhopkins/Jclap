#pragma once

// The plugin window: a Visage window embedded in the host's, with one root frame that fills it.
// This header doesn't include Visage, so plugin code can hold a Window without pulling it in.

#include <cstdint>
#include <functional>
#include <memory>

namespace visage {
class Frame;
}

namespace jclap::ui {

struct WindowOptions {
   // Initial size in logical points.
   int width = 800;
   int height = 600;
   // Keep width:height while the host resizes the window.
   bool fixedAspectRatio = true;
   // Smallest size, as a fraction of the initial one.
   float minimumScale = 0.5f;
};

class Window {
public:
   Window(std::unique_ptr<visage::Frame> root, const WindowOptions &options);
   ~Window();

   visage::Frame &root();
   const WindowOptions &options() const;

   void show(void *parentWindow);

   // Sizes in the units the CLAP host expects (logical points on macOS, pixels elsewhere).
   uint32_t width() const;
   uint32_t height() const;
   void setSize(uint32_t width, uint32_t height);
   void adjustSize(uint32_t *width, uint32_t *height) const;
   // Resizes to `zoom` times the initial size and asks the host to follow. A DesignFrame root offers
   // this in its background right-click menu.
   void setZoom(float zoom);

   // Linux only: the X11 connection fd the host polls for us.
   int posixFd() const;
   void processFdEvents();

   // Called when the window wants the host to resize it.
   std::function<void()> onResizeRequest;

private:
   struct Impl;
   std::unique_ptr<Impl> impl_;
};

// Draws a frame offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderToPng(visage::Frame &root, int width, int height, const char *pngPath);

} // namespace jclap::ui
