# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.11.0)

# Opus carries the audio, and mjansson/mdns (one public-domain header) finds tunnels on the network.
# Both are fetched here because only JTunnel uses them.
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
# A universal macOS build compiles every file for arm64 and x86_64, which Opus's per-file x86
# intrinsics can't do. The plain C build is fast enough for a stereo stream.
list(LENGTH CMAKE_OSX_ARCHITECTURES jtunnel_arch_count)
if(APPLE AND jtunnel_arch_count GREATER 1)
   set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(opus
   GIT_REPOSITORY https://github.com/xiph/opus.git
   GIT_TAG v1.5.2
   GIT_SHALLOW TRUE)
# SOURCE_SUBDIR points nowhere so only the header is fetched, not mdns's own CMake project.
FetchContent_Declare(mdns
   GIT_REPOSITORY https://github.com/mjansson/mdns.git
   GIT_TAG 1.4.3
   GIT_SHALLOW TRUE
   SOURCE_SUBDIR none)
FetchContent_MakeAvailable(opus mdns)

# Everything but the CLAP entry and the panel, so the tests can drive the engine without a host.
add_library(jtunnel_engine STATIC
   src/protocol.cpp
   src/codec.cpp
   src/receive_ring.cpp
   src/sender.cpp
   src/receiver.cpp
   src/net.cpp
   src/discovery.cpp
   src/link.cpp
   src/impair.cpp)
target_include_directories(jtunnel_engine PUBLIC src PRIVATE ${mdns_SOURCE_DIR})
target_compile_features(jtunnel_engine PUBLIC cxx_std_17)
target_link_libraries(jtunnel_engine PUBLIC jclap::core opus)
find_package(Threads REQUIRED)
target_link_libraries(jtunnel_engine PUBLIC Threads::Threads)
if(WIN32)
   target_link_libraries(jtunnel_engine PUBLIC ws2_32 iphlpapi)
   target_compile_definitions(jtunnel_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
if(NOT MSVC)
   target_compile_options(jtunnel_engine PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

jclap_add_plugin(jtunnel
   OUTPUT_NAME "JTunnel"
   BUNDLE_ID com.jeffmhopkins.jclap.jtunnel
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jtunnel PRIVATE src)
target_link_libraries(jtunnel PRIVATE jtunnel_engine jclap::ui)

# The engine on its own: packets, the resampler, and a sender and receiver on simulated clocks
# through a simulated Wi-Fi network (delay, jitter, loss, clock drift).
jclap_add_test(jtunnel-engine-tests SOURCES tests/test_engine.cpp)
target_link_libraries(jtunnel-engine-tests PRIVATE jtunnel_engine)

# Two whole plugins in one process, one sending and one receiving, over the loopback interface.
jclap_add_test(jtunnel-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jtunnel-plugin-tests PRIVATE src)
target_link_libraries(jtunnel-plugin-tests PRIVATE jtunnel_engine jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jtunnel-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jtunnel-screenshot PRIVATE src)
   target_link_libraries(jtunnel-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(jtunnel-screenshot-test TOOL jtunnel-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
endif()
