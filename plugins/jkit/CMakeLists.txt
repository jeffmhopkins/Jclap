# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.1.0)

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
FetchContent_MakeAvailable(miniz)

# The generator: tokenizer, ONNX Runtime (loaded at run time, never linked), Stable Audio Open Small,
# and the downloads. No Visage, so the tests can use it without a panel.
add_library(jkit_gen STATIC
   src/gen/tokenizer.cpp src/gen/ort.cpp src/gen/sao.cpp src/gen/fetch.cpp src/gen/studio.cpp)
target_include_directories(jkit_gen PUBLIC src PRIVATE third_party/onnxruntime)
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
endif()
