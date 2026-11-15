# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.3.0)

jclap_add_plugin(jarp
   OUTPUT_NAME "JArp"
   BUNDLE_ID com.jeffmhopkins.jclap.jarp
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jarp PRIVATE src)
target_link_libraries(jarp PRIVATE jclap::ui)

jclap_add_test(jarp-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(jarp-engine-tests PRIVATE src)
target_link_libraries(jarp-engine-tests PRIVATE jclap::core)

jclap_add_test(jarp-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jarp-tests PRIVATE src)
target_link_libraries(jarp-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jarp-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jarp-screenshot PRIVATE src)
   target_link_libraries(jarp-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(jarp-screenshot-test TOOL jarp-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
endif()
