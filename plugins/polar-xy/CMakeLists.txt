# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.14.0)

jclap_add_plugin(polar-xy
   OUTPUT_NAME "PolarXY"
   BUNDLE_ID com.jeffmhopkins.jclap.polar-xy
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(polar-xy PRIVATE src)
target_link_libraries(polar-xy PRIVATE jclap::ui)

jclap_add_test(polar-xy-tests SOURCES tests/test_polar.cpp)
target_include_directories(polar-xy-tests PRIVATE src)

# Host-style test of the whole plugin.
jclap_add_test(polar-xy-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(polar-xy-plugin-tests PRIVATE src)
target_link_libraries(polar-xy-plugin-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(polar-xy-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(polar-xy-screenshot PRIVATE src)
   target_link_libraries(polar-xy-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(polar-xy-screenshot-test TOOL polar-xy-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
endif()

# The VST3, for hosts without CLAP. The same plugin wrapped by clap-wrapper; see cmake/Vst3.cmake.
jclap_add_vst3(polar-xy-vst3
   OUTPUT_NAME "PolarXY"
   BUNDLE_ID com.jeffmhopkins.jclap.polar-xy
   SOURCES src/plugin.cpp src/editor.cpp)
if(TARGET polar-xy-vst3)
   target_include_directories(polar-xy-vst3 PRIVATE src)
   target_link_libraries(polar-xy-vst3 PRIVATE jclap::ui)

   # Loads the built .vst3 like a VST3 host and checks the CCs make it out.
   add_executable(polar-xy-vst3-tests tests/test_vst3.cpp)
   target_link_libraries(polar-xy-vst3-tests PRIVATE jclap_vst3_hosting)
   add_dependencies(polar-xy-vst3-tests polar-xy-vst3)
   add_test(NAME polar-xy-vst3-tests COMMAND polar-xy-vst3-tests ${CMAKE_BINARY_DIR}/vst3/PolarXY.vst3)
endif()
