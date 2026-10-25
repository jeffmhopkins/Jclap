# JSynth is a panel mock for now: it makes no sound, so it builds into build/preview, which CI checks
# but never ships. Move it to build/clap (drop OUTPUT_DIR) once it plays.
jclap_add_plugin(jsynth
   OUTPUT_NAME "JSynth"
   BUNDLE_ID com.jeffmhopkins.jclap.jsynth
   OUTPUT_DIR preview
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jsynth PRIVATE src)
target_link_libraries(jsynth PRIVATE jclap::ui)

jclap_add_test(jsynth-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jsynth-tests PRIVATE src)
target_link_libraries(jsynth-tests PRIVATE jclap::ui)

# Offscreen renders of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jsynth-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jsynth-screenshot PRIVATE src)
   target_link_libraries(jsynth-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(jsynth-screenshot-main-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png ARGS 0 0 1)
   jclap_add_screenshot_test(jsynth-screenshot-part-b-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-b.png ARGS 1 1 2)
   jclap_add_screenshot_test(jsynth-screenshot-map-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-map.png ARGS 0 1 3 0 14)
   jclap_add_screenshot_test(jsynth-screenshot-routes-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-routes.png ARGS 0 2 0 1)
   jclap_add_screenshot_test(jsynth-screenshot-hidden-test TOOL jsynth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-hidden.png ARGS 0 0 1 0 0 1200 780 1)
endif()
