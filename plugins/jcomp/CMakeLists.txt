# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.2.2)

jclap_add_plugin(jcomp
   OUTPUT_NAME "JComp"
   BUNDLE_ID com.jeffmhopkins.jclap.jcomp
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jcomp PRIVATE src)
target_link_libraries(jcomp PRIVATE jclap::ui)

jclap_add_test(jcomp-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(jcomp-engine-tests PRIVATE src)
target_link_libraries(jcomp-engine-tests PRIVATE jclap::core)

jclap_add_test(jcomp-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jcomp-tests PRIVATE src)
target_link_libraries(jcomp-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jcomp-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jcomp-screenshot PRIVATE src)
   target_link_libraries(jcomp-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(jcomp-screenshot-test TOOL jcomp-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(jcomp-screenshot-chords-test TOOL jcomp-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-chords.png ARGS chords)
   jclap_add_screenshot_test(jcomp-screenshot-edit-test TOOL jcomp-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-edit.png ARGS edit)
   jclap_add_screenshot_test(jcomp-screenshot-play-test TOOL jcomp-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-play.png ARGS play)
endif()
