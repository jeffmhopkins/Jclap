jclap_add_plugin(micromonsta2
   OUTPUT_NAME "MicroMonsta2CC"
   BUNDLE_ID com.jeffmhopkins.jclap.micromonsta2
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(micromonsta2 PRIVATE src)
target_link_libraries(micromonsta2 PRIVATE jclap::ui)

jclap_add_test(micromonsta2-tests SOURCES tests/test_cc_map.cpp)
target_include_directories(micromonsta2-tests PRIVATE src)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(micromonsta2-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(micromonsta2-plugin-tests PRIVATE src)
target_link_libraries(micromonsta2-plugin-tests PRIVATE clap jclap::ui)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(micromonsta2-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(micromonsta2-screenshot PRIVATE src)
   target_link_libraries(micromonsta2-screenshot PRIVATE jclap::ui)
endif()
