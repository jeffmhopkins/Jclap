#include "jclap/ui/window.h"

#include <visage/app.h>

namespace jclap::ui {

struct Window::Impl {
   visage::ApplicationWindow app;
   std::unique_ptr<visage::Frame> root;
   WindowOptions options;
};

Window::Window(std::unique_ptr<visage::Frame> root, const WindowOptions &options)
   : impl_(new Impl{{}, std::move(root), options}) {
   auto &app = impl_->app;
   app.addChild(impl_->root.get());
   app.onResize() += [this] { impl_->root->setBounds(impl_->app.localBounds()); };
   app.setWindowDimensions(options.width, options.height);
   app.setFixedAspectRatio(options.fixedAspectRatio);
   app.setMinimumDimensions(options.width * options.minimumScale, options.height * options.minimumScale);
   app.onWindowContentsResized() += [this] {
      if (onResizeRequest)
         onResizeRequest();
   };
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
void Window::setSize(uint32_t width, uint32_t height) { impl_->app.setWindowDimensions(width, height); }
#else
uint32_t Window::width() const { return static_cast<uint32_t>(impl_->app.nativeWidth()); }
uint32_t Window::height() const { return static_cast<uint32_t>(impl_->app.nativeHeight()); }
void Window::setSize(uint32_t width, uint32_t height) { impl_->app.setNativeWindowDimensions(width, height); }
#endif

void Window::adjustSize(uint32_t *width, uint32_t *height) const {
   impl_->app.adjustWindowDimensions(width, height, true, true);
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
