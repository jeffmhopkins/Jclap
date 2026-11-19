# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.1.0)

jclap_add_plugin(jdrummer
   OUTPUT_NAME "JDrummer"
   BUNDLE_ID com.jeffmhopkins.jclap.jdrummer
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jdrummer PRIVATE src)
target_link_libraries(jdrummer PRIVATE jclap::ui)

jclap_add_test(jdrummer-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(jdrummer-engine-tests PRIVATE src)
target_link_libraries(jdrummer-engine-tests PRIVATE jclap::core)

jclap_add_test(jdrummer-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jdrummer-tests PRIVATE src)
target_link_libraries(jdrummer-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jdrummer-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jdrummer-screenshot PRIVATE src)
   target_link_libraries(jdrummer-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(jdrummer-screenshot-test TOOL jdrummer-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(jdrummer-screenshot-edit-test TOOL jdrummer-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-edit.png ARGS edit)
   jclap_add_screenshot_test(jdrummer-screenshot-play-test TOOL jdrummer-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-play.png ARGS play)
endif()
