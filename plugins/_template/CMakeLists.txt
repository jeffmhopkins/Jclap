jclap_add_plugin(plugin_template
   OUTPUT_NAME "PluginTemplate"
   BUNDLE_ID com.jeffmhopkins.jclap.plugin-template
   OUTPUT_DIR template
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(plugin_template PRIVATE src)
target_link_libraries(plugin_template PRIVATE jclap::ui)

jclap_add_test(plugin_template-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(plugin_template-tests PRIVATE src)
target_link_libraries(plugin_template-tests PRIVATE jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(plugin_template-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(plugin_template-screenshot PRIVATE src)
   target_link_libraries(plugin_template-screenshot PRIVATE jclap::ui)
   jclap_add_screenshot_test(plugin_template-screenshot-test TOOL plugin_template-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
endif()
