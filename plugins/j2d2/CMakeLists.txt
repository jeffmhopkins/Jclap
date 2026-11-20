# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.3.0)

jclap_add_plugin(j2d2
   OUTPUT_NAME "J2D2"
   BUNDLE_ID com.jeffmhopkins.jclap.j2d2
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(j2d2 PRIVATE src)
target_link_libraries(j2d2 PRIVATE jclap::ui jclap::dsp)

jclap_add_test(j2d2-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(j2d2-tests PRIVATE src)
target_link_libraries(j2d2-tests PRIVATE jclap::ui jclap::dsp)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(j2d2-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(j2d2-screenshot PRIVATE src)
   target_link_libraries(j2d2-screenshot PRIVATE jclap::ui jclap::dsp)
   jclap_add_screenshot_test(j2d2-screenshot-test TOOL j2d2-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(j2d2-screenshot-drones-test TOOL j2d2-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-drones.png ARGS 1)
   jclap_add_screenshot_test(j2d2-screenshot-delay-test TOOL j2d2-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-delay.png ARGS 4)
endif()

# Renders the factory presets offline to a WAV, for listening and level checks.
if(JCLAP_BUILD_TOOLS)
   add_executable(j2d2-render tools/render.cpp)
   target_include_directories(j2d2-render PRIVATE src)
   target_link_libraries(j2d2-render PRIVATE jclap::ui jclap::dsp)
endif()
