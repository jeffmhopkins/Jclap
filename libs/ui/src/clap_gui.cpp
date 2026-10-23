#include "jclap/ui/clap_gui.h"

#include <cstring>

namespace jclap::ui {
namespace {

#if defined(_WIN32)
const char *const kWindowApi = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
const char *const kWindowApi = CLAP_WINDOW_API_COCOA;
#else
const char *const kWindowApi = CLAP_WINDOW_API_X11;
#endif

} // namespace

void ClapGui::init(const clap_host_t *host) {
   host_ = host;
   hostGui_ = static_cast<const clap_host_gui_t *>(host->get_extension(host, CLAP_EXT_GUI));
   auto *fd = static_cast<const clap_host_posix_fd_support_t *>(host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
   if (fd && fd->register_fd && fd->unregister_fd)
      hostFd_ = fd;
}

bool ClapGui::isApiSupported(const char *api, bool isFloating) { return !isFloating && !std::strcmp(api, kWindowApi); }

bool ClapGui::getPreferredApi(const char **api, bool *isFloating) {
   *api = kWindowApi;
   *isFloating = false;
   return true;
}

bool ClapGui::create(const char *api, bool isFloating) {
   if (!isApiSupported(api, isFloating) || !createWindow)
      return false;
   if (!window_) {
      window_ = createWindow();
      window_->onResizeRequest = [this] {
         if (hostGui_)
            hostGui_->request_resize(host_, window_->width(), window_->height());
      };
   }
   return true;
}

void ClapGui::destroy() {
   if (fdRegistered_ && window_) {
      hostFd_->unregister_fd(host_, window_->posixFd());
      fdRegistered_ = false;
   }
   window_.reset();
}

bool ClapGui::getSize(uint32_t *width, uint32_t *height) const {
   if (!window_)
      return false;
   *width = window_->width();
   *height = window_->height();
   return true;
}

bool ClapGui::getResizeHints(clap_gui_resize_hints_t *hints) const {
   if (!window_)
      return false;
   hints->can_resize_horizontally = true;
   hints->can_resize_vertically = true;
   hints->preserve_aspect_ratio = window_->options().fixedAspectRatio;
   hints->aspect_ratio_width = window_->width();
   hints->aspect_ratio_height = window_->height();
   return true;
}

bool ClapGui::adjustSize(uint32_t *width, uint32_t *height) const {
   if (!window_)
      return false;
   window_->adjustSize(width, height);
   return true;
}

bool ClapGui::setSize(uint32_t width, uint32_t height) {
   if (!window_)
      return false;
   window_->setSize(width, height);
   return true;
}

bool ClapGui::setParent(const clap_window_t *window) {
   if (!window_)
      return false;
#if defined(__APPLE__) || defined(_WIN32)
   window_->show(window->ptr);
#else
   window_->show(reinterpret_cast<void *>(window->x11));
   if (hostFd_ && !fdRegistered_)
      fdRegistered_ = hostFd_->register_fd(host_, window_->posixFd(),
                                           CLAP_POSIX_FD_READ | CLAP_POSIX_FD_WRITE | CLAP_POSIX_FD_ERROR);
#endif
   return true;
}

void ClapGui::onFd() {
   if (window_)
      window_->processFdEvents();
}

} // namespace jclap::ui
