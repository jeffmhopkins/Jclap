#include "jclap/ui/window.h"

#include <algorithm>
#include <cmath>

#include <visage/app.h>

#include "jclap/ui/design_frame.h"
#include "jclap/ui/theme.h"

namespace jclap::ui {
namespace {

// CLAP sizes are logical points on macOS and physical pixels elsewhere.
double unitsPerPoint(const visage::Frame &app) {
#if defined(__APPLE__)
   (void)app;
   return 1.0;
#else
   return app.dpiScale() > 0.0f ? app.dpiScale() : 1.0;
#endif
}

double pixelsPerUnit(const visage::Frame &app) {
#if defined(__APPLE__)
   return app.dpiScale() > 0.0f ? app.dpiScale() : 1.0;
#else
   (void)app;
   return 1.0;
#endif
}

} // namespace

struct Window::Impl {
   visage::ApplicationWindow app;
   std::unique_ptr<visage::Frame> root;
   WindowOptions options;
};

Window::Window(std::unique_ptr<visage::Frame> root, const WindowOptions &options)
   : impl_(new Impl{{}, std::move(root), options}) {
   auto &app = impl_->app;
   app.addChild(impl_->root.get());
   app.onResize() += [this] { layoutRoot(); };
   // Fills the bars left around a letterboxed DesignFrame.
   app.onDraw() += [this](visage::Canvas &canvas) {
      canvas.setColor(defaultTheme().background);
      canvas.fill(0, 0, impl_->app.width(), impl_->app.height());
   };
   app.setWindowDimensions(options.width, options.height);
   app.setFixedAspectRatio(options.fixedAspectRatio);
   app.setMinimumDimensions(options.width * options.minimumScale, options.height * options.minimumScale);
   // No onWindowContentsResized hook: it fires for every resize, the host's own included, and
   // echoing those back as request_resize makes hosts recurse or fight over the size.
   if (auto *design = dynamic_cast<DesignFrame *>(impl_->root.get()))
      design->onZoom = [this](float zoom) { setZoom(zoom); };
}

// A DesignFrame keeps its aspect ratio, centred, when the host gives the window another shape.
void Window::layoutRoot() {
   visage::ApplicationWindow &app = impl_->app;
   auto *design = dynamic_cast<DesignFrame *>(impl_->root.get());
   if (!design) {
      impl_->root->setBounds(app.localBounds());
      return;
   }
   const float w = app.width(), h = app.height();
   const float s = std::min(w / design->designWidth(), h / design->designHeight());
   float fitW = design->designWidth() * s, fitH = design->designHeight() * s;
   // Within two pixels of the window's own shape, fill it instead of drawing hairline bars.
   const float slack = 2.0f / (app.dpiScale() > 0.0f ? app.dpiScale() : 1.0f);
   if (w - fitW < slack && h - fitH < slack) {
      fitW = w;
      fitH = h;
   }
   design->setBounds((w - fitW) * 0.5f, (h - fitH) * 0.5f, fitW, fitH);
   const WindowOptions &o = impl_->options;
   design->currentZoom = std::min(w / std::max(o.width, 1), h / std::max(o.height, 1));
}

Window::Limits Window::limits() const {
   const WindowOptions &o = impl_->options;
   const double u = unitsPerPoint(impl_->app);
   const auto cap = static_cast<int64_t>(kMaxWindowPixels / pixelsPerUnit(impl_->app));
   Limits l;
   l.maxWidth = std::min(static_cast<int64_t>(o.width * u * o.maximumScale + 1e-6), cap);
   l.maxHeight = std::min(static_cast<int64_t>(o.height * u * o.maximumScale + 1e-6), cap);
   l.minWidth = std::clamp(static_cast<int64_t>(std::ceil(o.width * u * o.minimumScale - 1e-6)), int64_t{1}, l.maxWidth);
   l.minHeight = std::clamp(static_cast<int64_t>(std::ceil(o.height * u * o.minimumScale - 1e-6)), int64_t{1}, l.maxHeight);
   return l;
}

void Window::setZoom(float zoom) {
   const WindowOptions &o = impl_->options;
   const double u = unitsPerPoint(impl_->app);
   auto w = static_cast<uint32_t>(std::lround(o.width * zoom * u));
   auto h = static_cast<uint32_t>(std::lround(o.height * zoom * u));
   adjustSize(&w, &h);
   const uint32_t oldW = width(), oldH = height();
   if (w == oldW && h == oldH)
      return;
   if (onResizeRequest && !onResizeRequest(w, h))
      return;
   // The host may have called set_size from inside request_resize; that size wins.
   if (width() == oldW && height() == oldH)
      setSize(w, h);
}

Window::~Window() {
   impl_->app.removeChild(impl_->root.get());
   impl_->app.close();
}

visage::Frame &Window::root() { return *impl_->root; }
const WindowOptions &Window::options() const { return impl_->options; }

void Window::show(void *parentWindow) { impl_->app.show(parentWindow); }

#if defined(__APPLE__)
uint32_t Window::width() const { return static_cast<uint32_t>(impl_->app.width()); }
uint32_t Window::height() const { return static_cast<uint32_t>(impl_->app.height()); }
#else
uint32_t Window::width() const { return static_cast<uint32_t>(impl_->app.nativeWidth()); }
uint32_t Window::height() const { return static_cast<uint32_t>(impl_->app.nativeHeight()); }
#endif

bool Window::setSize(uint32_t width, uint32_t height) {
   // A pixel under the minimum is allowed for the aspect ratio's rounding in adjustSize.
   const Limits l = limits();
   if (width + int64_t{1} < l.minWidth || height + int64_t{1} < l.minHeight || width > l.maxWidth ||
       height > l.maxHeight)
      return false;
#if defined(__APPLE__)
   impl_->app.setWindowDimensions(static_cast<int>(width), static_cast<int>(height));
#else
   impl_->app.setNativeWindowDimensions(static_cast<int>(width), static_cast<int>(height));
#endif
   return true;
}

// Our own fit, not Visage's: that one can grow past the size given, has no maximum before the
// window exists, and overflows on huge sizes. Everything here is int64 so any uint32 is safe.
void Window::adjustSize(uint32_t *width, uint32_t *height) const {
   const WindowOptions &o = impl_->options;
   const Limits l = limits();
   int64_t w = *width, h = *height;
   if (o.fixedAspectRatio) {
      const int64_t dw = std::max(o.width, 1), dh = std::max(o.height, 1);
      // Heights follow widths (rounded down), so the widest width whose height fits in `fit` is:
      const auto widestFor = [&](int64_t fit) { return ((fit + 1) * dw - 1) / dh; };
      w = std::min(w, widestFor(h));
      w = std::min(std::max(w, l.minWidth), std::min(l.maxWidth, widestFor(l.maxHeight)));
      h = w * dh / dw;
   }
   else {
      w = std::min(std::max(w, l.minWidth), l.maxWidth);
      h = std::min(std::max(h, l.minHeight), l.maxHeight);
   }
   *width = static_cast<uint32_t>(w);
   *height = static_cast<uint32_t>(h);
}

int Window::posixFd() const { return impl_->app.window() ? impl_->app.window()->posixFd() : -1; }

void Window::processFdEvents() {
   if (impl_->app.window())
      impl_->app.window()->processPluginFdEvents();
}

bool renderToPng(visage::Frame &root, int width, int height, const char *pngPath) {
   visage::ApplicationEditor editor;
   editor.addChild(&root);
   editor.onResize() += [&] { root.setBounds(editor.localBounds()); };
   editor.setWindowless(width, height);
   root.setBounds(editor.localBounds());
   editor.takeScreenshot().save(pngPath);
   editor.removeChild(&root);
   return true;
}

} // namespace jclap::ui
