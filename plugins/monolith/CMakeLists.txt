jclap_add_plugin(monolith
   OUTPUT_NAME "Monolith"
   BUNDLE_ID com.jeffmhopkins.jclap.monolith
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(monolith PRIVATE src)
target_link_libraries(monolith PRIVATE jclap::ui)

jclap_add_test(monolith-engine-tests SOURCES tests/test_engine.cpp)
target_include_directories(monolith-engine-tests PRIVATE src)
target_link_libraries(monolith-engine-tests PRIVATE jclap::core)

jclap_add_test(monolith-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(monolith-tests PRIVATE src)
target_link_libraries(monolith-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(monolith-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(monolith-screenshot PRIVATE src)
   target_link_libraries(monolith-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(monolith-screenshot-test TOOL monolith-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(monolith-stage-screenshot-test TOOL monolith-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/stage.png ARGS 0 1200 680 listen)
endif()
