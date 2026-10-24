#pragma once

// The clap_entry symbol and plugin factory for one or more Plugin subclasses. Each needs a
// `static const clap_plugin_descriptor_t descriptor` and a constructor taking the host.
//
//    JCLAP_ENTRY(MyPlugin)            // or JCLAP_ENTRY(PluginA, PluginB)

#include <clap/clap.h>

#include <cstring>

#include "jclap/core/plugin.h"

namespace jclap::core {

template <typename... Plugins>
struct Entry {
   static uint32_t count(const clap_plugin_factory_t *) { return sizeof...(Plugins); }

   static const clap_plugin_descriptor_t *descriptor(const clap_plugin_factory_t *, uint32_t index) {
      static const clap_plugin_descriptor_t *const descriptors[] = {&Plugins::descriptor...};
      return index < sizeof...(Plugins) ? descriptors[index] : nullptr;
   }

   static const clap_plugin_t *create(const clap_plugin_factory_t *, const clap_host_t *host, const char *id) {
      if (!clap_version_is_compatible(host->clap_version))
         return nullptr;
      const clap_plugin_t *created = nullptr;
      ((created = created ? created : tryCreate<Plugins>(host, id)), ...);
      return created;
   }

   static const void *factory(const char *id) {
      static const clap_plugin_factory_t kFactory = {count, descriptor, create};
      return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &kFactory : nullptr;
   }

   static bool init(const char *) { return true; }
   static void deinit() {}

private:
   template <typename P>
   static const clap_plugin_t *tryCreate(const clap_host_t *host, const char *id) {
      if (std::strcmp(id, P::descriptor.id))
         return nullptr;
      // A constructor that throws (out of memory, say) is a failed create, not an exception
      // unwinding into the host.
      try {
         return (new P(host))->clapPlugin();
      } catch (...) {
         return nullptr;
      }
   }
};

} // namespace jclap::core

#define JCLAP_ENTRY(...)                                                                                  \
   extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {                                        \
      CLAP_VERSION_INIT, jclap::core::Entry<__VA_ARGS__>::init, jclap::core::Entry<__VA_ARGS__>::deinit, \
      jclap::core::Entry<__VA_ARGS__>::factory};
