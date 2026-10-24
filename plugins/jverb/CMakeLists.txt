jclap_add_plugin(jverb
   OUTPUT_NAME "JVerb"
   BUNDLE_ID com.jeffmhopkins.jclap.jverb
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jverb PRIVATE src)
target_link_libraries(jverb PRIVATE jclap::ui jclap::dsp)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(jverb-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jverb-tests PRIVATE src)
target_link_libraries(jverb-tests PRIVATE jclap::ui jclap::dsp)

# The tank on its own.
jclap_add_test(jverb-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(jverb-engine-tests PRIVATE src)
target_link_libraries(jverb-engine-tests PRIVATE jclap::dsp jclap::core)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jverb-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jverb-screenshot PRIVATE src)
   target_link_libraries(jverb-screenshot PRIVATE jclap::ui jclap::dsp)
   jclap_add_screenshot_test(jverb-screenshot-test TOOL jverb-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(jverb-screenshot-six-test TOOL jverb-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-six.png ARGS 6)
   jclap_add_screenshot_test(jverb-screenshot-orbit-test TOOL jverb-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-orbit.png ARGS 3 2)
endif()
