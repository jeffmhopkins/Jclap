# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.11.0)

jclap_add_plugin(jsynth
   OUTPUT_NAME "JSynth"
   BUNDLE_ID com.jeffmhopkins.jclap.jsynth
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jsynth PRIVATE src)
target_link_libraries(jsynth PRIVATE jclap::ui jclap::dsp)

jclap_add_test(jsynth-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jsynth-tests PRIVATE src)
target_link_libraries(jsynth-tests PRIVATE jclap::ui jclap::dsp)

# The engine alone: sound, determinism, notes and CPU, with no panel.
jclap_add_test(jsynth-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(jsynth-engine-tests PRIVATE src)
target_link_libraries(jsynth-engine-tests PRIVATE jclap::core jclap::ui)

# Offscreen renders of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jsynth-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jsynth-screenshot PRIVATE src)
   target_link_libraries(jsynth-screenshot PRIVATE jclap::ui jclap::dsp)
   # Records a phrase through the engine: audio, or video of the panel with the audio.
   add_executable(jsynth-record tools/record.cpp src/editor.cpp)
   target_include_directories(jsynth-record PRIVATE src)
   target_link_libraries(jsynth-record PRIVATE jclap::ui jclap::dsp)
   jclap_add_screenshot_test(jsynth-screenshot-main-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png ARGS 0)
   jclap_add_screenshot_test(jsynth-screenshot-part-b-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-b.png ARGS 1)
   jclap_add_screenshot_test(jsynth-screenshot-map-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-map.png ARGS 0 0 12)
   jclap_add_screenshot_test(jsynth-screenshot-routes-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-routes.png ARGS 0 1)
   jclap_add_screenshot_test(jsynth-screenshot-sky-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-sky.png ARGS 0 0 0 1648 940 1)
   jclap_add_screenshot_test(jsynth-screenshot-touch-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-touch.png ARGS 0 0 0 1648 940 0 3)
endif()
