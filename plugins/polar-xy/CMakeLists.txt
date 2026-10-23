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
