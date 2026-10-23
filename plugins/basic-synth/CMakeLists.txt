jclap_add_plugin(basic_synth
   OUTPUT_NAME "BasicSynth"
   BUNDLE_ID com.jeffmhopkins.jclap.basic-synth
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(basic_synth PRIVATE src)
target_link_libraries(basic_synth PRIVATE jclap::ui jclap::dsp)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(basic_synth-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(basic_synth-tests PRIVATE src)
target_link_libraries(basic_synth-tests PRIVATE jclap::ui jclap::dsp)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(basic_synth-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(basic_synth-screenshot PRIVATE src)
   target_link_libraries(basic_synth-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(basic_synth-screenshot-test TOOL basic_synth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(basic_synth-screenshot-grid-test TOOL basic_synth-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-grid.png ARGS 1)
endif()
