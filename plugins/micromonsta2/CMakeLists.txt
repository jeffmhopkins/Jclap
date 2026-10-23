# Visage draws the knob panel. It has no releases yet, so it is pinned to a commit.
FetchContent_Declare(visage
   GIT_REPOSITORY https://github.com/VitalAudio/visage.git
   GIT_TAG 7654e32a84f95aacf9d15390272a47d2952d9565)
FetchContent_MakeAvailable(visage)

jclap_add_plugin(micromonsta2
   OUTPUT_NAME "MicroMonsta2CC"
   BUNDLE_ID com.jeffmhopkins.jclap.micromonsta2
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(micromonsta2 PRIVATE src)
target_link_libraries(micromonsta2 PRIVATE visage VisageEmbeddedFonts)

jclap_add_test(micromonsta2-tests SOURCES tests/test_cc_map.cpp)
target_include_directories(micromonsta2-tests PRIVATE src)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(micromonsta2-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(micromonsta2-plugin-tests PRIVATE src)
target_link_libraries(micromonsta2-plugin-tests PRIVATE clap visage VisageEmbeddedFonts)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
option(JCLAP_BUILD_TOOLS "Build developer tools" OFF)
if(JCLAP_BUILD_TOOLS)
   add_executable(micromonsta2-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(micromonsta2-screenshot PRIVATE src)
   target_link_libraries(micromonsta2-screenshot PRIVATE visage VisageEmbeddedFonts)
endif()
