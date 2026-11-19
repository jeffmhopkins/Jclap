# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.7.0)

# miniz unpacks ONNX Runtime's library from Microsoft's NuGet package (a zip) on first use.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
set(AMALGAMATE_SOURCES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz
   GIT_REPOSITORY https://github.com/richgel999/miniz.git
   GIT_TAG 3.0.2)
# miniz 3.0.2 asks for CMake 2.8; CMake 4 refuses that unless told the policies it may assume.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(miniz)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# pocketfft (BSD-3, numpy's own FFT), header-only, for ORB's renderer.
FetchContent_Declare(pocketfft
   GIT_REPOSITORY https://github.com/mreineck/pocketfft.git
   GIT_TAG 4d865cba24359b05b37da4e75aee24498c3b78e7)
FetchContent_MakeAvailable(pocketfft) # no CMakeLists.txt: this only fetches it

# The generator: tokenizer, ONNX Runtime (loaded at run time, never linked), Stable Audio Open Small,
# ORB (its planner and renderer), and the downloads. No Visage, so the tests can use it without a panel.
add_library(jkit_gen STATIC
   src/gen/tokenizer.cpp src/gen/ort.cpp src/gen/sao.cpp src/gen/fetch.cpp src/gen/studio.cpp
   src/gen/orb/dsp.cpp src/gen/orb/render.cpp src/gen/orb/planner.cpp src/gen/orb_engine.cpp)
target_include_directories(jkit_gen PUBLIC src PRIVATE third_party/onnxruntime ${pocketfft_SOURCE_DIR})
target_compile_features(jkit_gen PUBLIC cxx_std_17)
target_link_libraries(jkit_gen PUBLIC jclap::ui PRIVATE miniz)
if(WIN32)
   target_link_libraries(jkit_gen PRIVATE winhttp)
else()
   target_link_libraries(jkit_gen PRIVATE ${CMAKE_DL_LIBS})
endif()
if(NOT MSVC)
   target_compile_options(jkit_gen PRIVATE -Wall -Wextra)
endif()

jclap_add_plugin(jkit
   OUTPUT_NAME "JKit"
   BUNDLE_ID com.jeffmhopkins.jclap.jkit
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jkit PRIVATE src)
target_link_libraries(jkit PRIVATE jkit_gen jclap::ui jclap::dsp)

jclap_add_test(jkit-gen-tests SOURCES tests/test_gen.cpp)
target_link_libraries(jkit-gen-tests PRIVATE jkit_gen)

jclap_add_test(jkit-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jkit-tests PRIVATE src)
target_link_libraries(jkit-tests PRIVATE jkit_gen jclap::ui jclap::dsp)

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jkit-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jkit-screenshot PRIVATE src)
   target_link_libraries(jkit-screenshot PRIVATE jclap::ui jclap::dsp)
   jclap_add_screenshot_test(jkit-screenshot-test TOOL jkit-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   jclap_add_screenshot_test(jkit-screenshot-kit-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-kit.png ARGS 1)
   jclap_add_screenshot_test(jkit-screenshot-first-run-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-first-run.png ARGS 2)
   jclap_add_screenshot_test(jkit-screenshot-orb-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-orb.png ARGS 3)
   jclap_add_screenshot_test(jkit-screenshot-engines-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-engines.png ARGS 4)
   jclap_add_screenshot_test(jkit-screenshot-kit-mode-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-kit-mode.png ARGS 5)
   jclap_add_screenshot_test(jkit-screenshot-kit-dialog-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-whole-kit.png ARGS 6)
   jclap_add_screenshot_test(jkit-screenshot-takes-test TOOL jkit-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel-takes.png ARGS 8)
endif()
