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
   auto *timer = static_cast<const clap_host_timer_support_t *>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
   if (timer && timer->register_timer && timer->unregister_timer)
      hostTimer_ = timer;
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
      if (!window_)
         return false;
      // Without the host's gui extension there is no one to ask, so the window just resizes.
      window_->onResizeRequest = [this](uint32_t w, uint32_t h) {
         return !hostGui_ || hostGui_->request_resize(host_, w, h);
      };
      if (lastWidth_ > 0 && lastHeight_ > 0) {
         uint32_t w = lastWidth_, h = lastHeight_;
         window_->adjustSize(&w, &h);
         window_->setSize(w, h);
      }
   }
   return true;
}

void ClapGui::unregisterFd() {
   if (fd_ >= 0)
      hostFd_->unregister_fd(host_, fd_);
   fd_ = -1;
}

void ClapGui::unregisterTimer() {
   if (timer_ != CLAP_INVALID_ID)
      hostTimer_->unregister_timer(host_, timer_);
   timer_ = CLAP_INVALID_ID;
}

void ClapGui::destroy() {
   unregisterFd();
   unregisterTimer();
   if (window_) {
      lastWidth_ = window_->width();
      lastHeight_ = window_->height();
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
   // The design ratio, which adjustSize keeps; the current size would drift with rounding.
   hints->preserve_aspect_ratio = window_->options().fixedAspectRatio;
   hints->aspect_ratio_width = static_cast<uint32_t>(window_->options().width);
   hints->aspect_ratio_height = static_cast<uint32_t>(window_->options().height);
   return true;
}

bool ClapGui::adjustSize(uint32_t *width, uint32_t *height) const {
   if (!window_)
      return false;
   window_->adjustSize(width, height);
   return true;
}

bool ClapGui::setSize(uint32_t width, uint32_t height) {
   return window_ && window_->setSize(width, height);
}

bool ClapGui::setParent(const clap_window_t *window) {
   if (!window_)
      return false;
#if defined(__APPLE__) || defined(_WIN32)
   window_->show(window->ptr);
#else
   // Every show() opens a new X connection and closes the old one, so a second set_parent needs
   // the fd swapped. Only READ: the socket is nearly always writable, and asking for WRITE makes
   // the host call on_fd nonstop.
   unregisterFd();
   unregisterTimer();
   window_->show(reinterpret_cast<void *>(window->x11));
   const int fd = window_->posixFd();
   if (hostFd_ && fd >= 0 && hostFd_->register_fd(host_, fd, CLAP_POSIX_FD_READ | CLAP_POSIX_FD_ERROR))
      fd_ = fd;
   // No fd support: poll the connection from a host timer instead, about once a frame.
   else if (!hostTimer_ || !hostTimer_->register_timer(host_, 16, &timer_))
      timer_ = CLAP_INVALID_ID;
   // Nothing would ever service the window, so it would sit there unpainted.
   if (fd_ < 0 && timer_ == CLAP_INVALID_ID)
      return false;
#endif
   return true;
}

bool ClapGui::setScale(double scale) { return window_ && window_->setHostScale(scale); }

bool ClapGui::show() {
   if (window_)
      window_->setVisible(true);
   return window_ != nullptr;
}

bool ClapGui::hide() {
   if (window_)
      window_->setVisible(false);
   return window_ != nullptr;
}

void ClapGui::onFd() {
   if (window_)
      window_->processFdEvents();
}

void ClapGui::onTimer(clap_id id) {
   if (window_ && id == timer_)
      window_->processFdEvents();
}

} // namespace jclap::ui
