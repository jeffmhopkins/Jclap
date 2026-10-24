#pragma once

// The CLAP gui, posix-fd-support and timer-support extensions for a plugin with one embedded
// jclap::ui::Window. On Linux the window's events come through the host's posix-fd support, or
// failing that a 16 ms host timer.
// With a jclap::core::Plugin subclass:
//
//    class MyPlugin : public jclap::core::Plugin { ... jclap::ui::ClapGui gui; };
//
//    init():      gui.init(host()); gui.createWindow = [this] { return createEditor(*this); };
//    extension(): if (auto *ext = jclap::ui::ClapGui::extensionFor<MyPlugin, &MyPlugin::gui>(id)) return ext;
//
// Without one, pass a function that finds the ClapGui: ClapGui::extension<guiOf>(id).
//
// All of it runs on the main thread, as CLAP requires for these extensions.

#include <clap/clap.h>

#include <functional>
#include <memory>
#include <string_view>

#include "jclap/core/plugin.h"
#include "jclap/ui/window.h"

namespace jclap::ui {

class ClapGui {
public:
   ClapGui() = default;
   ~ClapGui() { destroy(); }
   ClapGui(const ClapGui &) = delete;
   ClapGui &operator=(const ClapGui &) = delete;

   // Looks up the host's gui, posix-fd and timer extensions. Call from the plugin's init().
   void init(const clap_host_t *host);

   // Builds the window when the host opens the GUI.
   std::function<std::unique_ptr<Window>()> createWindow;

   // The open window, or null.
   Window *window() const { return window_.get(); }

   // Closes the window. Also done on destruction, in case the host didn't.
   void destroy();

   // The gui / posix-fd-support extension struct for `id`, or null. `Lookup` finds the plugin's
   // ClapGui from its clap_plugin_t.
   template <ClapGui *(*Lookup)(const clap_plugin_t *)>
   static const void *extension(const char *id);

   // The same for a jclap::core::Plugin subclass P holding the ClapGui in member M.
   template <typename P, ClapGui P::*M>
   static const void *extensionFor(const char *id) {
      return extension<&memberOf<P, M>>(id);
   }

   // The extension calls, one per clap_plugin_gui_t entry.
   static bool isApiSupported(const char *api, bool isFloating);
   static bool getPreferredApi(const char **api, bool *isFloating);
   bool create(const char *api, bool isFloating);
   bool getSize(uint32_t *width, uint32_t *height) const;
   bool getResizeHints(clap_gui_resize_hints_t *hints) const;
   bool adjustSize(uint32_t *width, uint32_t *height) const;
   bool setSize(uint32_t width, uint32_t height);
   bool setParent(const clap_window_t *window);
   bool setScale(double scale);
   bool show();
   bool hide();
   void onFd();
   void onTimer(clap_id id);

private:
   template <typename P, ClapGui P::*M>
   static ClapGui *memberOf(const clap_plugin_t *p) {
      return &(static_cast<P *>(core::Plugin::from(p))->*M);
   }

   void unregisterFd();
   void unregisterTimer();

   const clap_host_t *host_ = nullptr;
   const clap_host_gui_t *hostGui_ = nullptr;
   const clap_host_posix_fd_support_t *hostFd_ = nullptr;
   const clap_host_timer_support_t *hostTimer_ = nullptr;
   std::unique_ptr<Window> window_;
   // The fd registered with the host, or -1.
   int fd_ = -1;
   // The fallback timer registered with the host, or CLAP_INVALID_ID.
   clap_id timer_ = CLAP_INVALID_ID;
   // The size the window had when it was last closed, so reopening the editor keeps the user's UI
   // size instead of going back to 100% (0 until then).
   uint32_t lastWidth_ = 0, lastHeight_ = 0;
};

template <ClapGui *(*Lookup)(const clap_plugin_t *)>
const void *ClapGui::extension(const char *id) {
   static const clap_plugin_gui_t gui = {
      [](const clap_plugin_t *, const char *api, bool floating) { return isApiSupported(api, floating); },
      [](const clap_plugin_t *, const char **api, bool *floating) { return getPreferredApi(api, floating); },
      [](const clap_plugin_t *p, const char *api, bool floating) { return Lookup(p)->create(api, floating); },
      [](const clap_plugin_t *p) { Lookup(p)->destroy(); },
      // Linux takes the host's scale; macOS and Windows use the OS's per-monitor scale, as CLAP
      // allows, so it is refused there.
      [](const clap_plugin_t *p, double scale) { return Lookup(p)->setScale(scale); },
      [](const clap_plugin_t *p, uint32_t *w, uint32_t *h) { return Lookup(p)->getSize(w, h); },
      [](const clap_plugin_t *) { return true; },
      [](const clap_plugin_t *p, clap_gui_resize_hints_t *hints) { return Lookup(p)->getResizeHints(hints); },
      [](const clap_plugin_t *p, uint32_t *w, uint32_t *h) { return Lookup(p)->adjustSize(w, h); },
      [](const clap_plugin_t *p, uint32_t w, uint32_t h) { return Lookup(p)->setSize(w, h); },
      [](const clap_plugin_t *p, const clap_window_t *window) { return Lookup(p)->setParent(window); },
      [](const clap_plugin_t *, const clap_window_t *) { return false; },
      [](const clap_plugin_t *, const char *) {},
      [](const clap_plugin_t *p) { return Lookup(p)->show(); },
      [](const clap_plugin_t *p) { return Lookup(p)->hide(); },
   };
   if (id == std::string_view(CLAP_EXT_GUI))
      return &gui;
#if !defined(__APPLE__) && !defined(_WIN32)
   static const clap_plugin_posix_fd_support_t fd = {
      [](const clap_plugin_t *p, int, clap_posix_fd_flags_t) { Lookup(p)->onFd(); },
   };
   if (id == std::string_view(CLAP_EXT_POSIX_FD_SUPPORT))
      return &fd;
   static const clap_plugin_timer_support_t timer = {
      [](const clap_plugin_t *p, clap_id timerId) { Lookup(p)->onTimer(timerId); },
   };
   if (id == std::string_view(CLAP_EXT_TIMER_SUPPORT))
      return &timer;
#endif
   return nullptr;
}

} // namespace jclap::ui
