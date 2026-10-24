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
   // Smallest and largest size, as a fraction of the initial one. Sizes are also capped at
   // kMaxWindowPixels a side, so the GPU never gets a texture it can't make.
   float minimumScale = 0.5f;
   float maximumScale = 4.0f;
};

// Largest window side in physical pixels. GPUs back to D3D 10 / GL 4 / Metal on Intel Macs
// support textures at least this big.
constexpr int kMaxWindowPixels = 8192;

class Window {
public:
   Window(std::unique_ptr<visage::Frame> root, const WindowOptions &options);
   ~Window();

   visage::Frame &root();
   const WindowOptions &options() const;

   void show(void *parentWindow);
   // Linux: stop and restart drawing while the host has the editor hidden.
   void setVisible(bool visible);
   // Linux: the host's UI scale (CLAP set_scale), used instead of the desktop's from the next
   // show(). Keeps the window's zoom. False elsewhere, where the OS's scale is used.
   bool setHostScale(double scale);

   // Sizes in the units the CLAP host expects (logical points on macOS, pixels elsewhere).
   uint32_t width() const;
   uint32_t height() const;
   // Applies a size the host chose. Returns false, and keeps the current size, if it is outside the
   // minimum and maximum (a host should pass it through adjustSize first).
   bool setSize(uint32_t width, uint32_t height);
   // Makes a size usable: the largest one that fits in it at the initial aspect ratio (when fixed),
   // within the minimum and maximum.
   void adjustSize(uint32_t *width, uint32_t *height) const;
   // Asks the host for `zoom` times the initial size (through onResizeRequest), and resizes only if
   // it agrees. A DesignFrame root offers this in its background right-click menu.
   void setZoom(float zoom);

   // Linux only: the X11 connection fd the host polls for us.
   int posixFd() const;
   void processFdEvents();

   // Called when the window wants the host to resize it (zoom only, never for a resize the host
   // made). Returns whether to go ahead; with no handler the window resizes by itself.
   std::function<bool(uint32_t width, uint32_t height)> onResizeRequest;

private:
   struct Limits {
      int64_t minWidth, minHeight, maxWidth, maxHeight;
   };
   Limits limits() const;
   void layoutRoot();

   struct Impl;
   std::unique_ptr<Impl> impl_;
};

// Draws a frame offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderToPng(visage::Frame &root, int width, int height, const char *pngPath);

} // namespace jclap::ui
