# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.3.0)

jclap_add_plugin(matrixbrute
   OUTPUT_NAME "MatrixBruteCC"
   BUNDLE_ID com.jeffmhopkins.jclap.matrixbrute
   SOURCES src/plugin.cpp src/editor.cpp src/synth_link.cpp)
target_include_directories(matrixbrute PRIVATE src)
target_link_libraries(matrixbrute PRIVATE jclap::ui)
if(WIN32)
   target_link_libraries(matrixbrute PRIVATE ws2_32)
endif()

jclap_add_test(matrixbrute-tests SOURCES tests/test_cc_map.cpp)
target_include_directories(matrixbrute-tests PRIVATE src)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(matrixbrute-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp src/synth_link.cpp)
target_include_directories(matrixbrute-plugin-tests PRIVATE src)
target_link_libraries(matrixbrute-plugin-tests PRIVATE jclap::ui)
if(WIN32)
   target_link_libraries(matrixbrute-plugin-tests PRIVATE ws2_32)
endif()

# The USB capture decoder (tools/mb_usb_extract.py), on made-up captures.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
   add_test(NAME matrixbrute-usb-extract COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_usb_extract.py)
   # The preset bank is generated from the dump; this decodes it again, checks the decoding against
   # the calibration reads, and fails if src/factory_bank.inc is stale.
   add_test(NAME matrixbrute-bank COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/mb_bank.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/dump ${CMAKE_CURRENT_SOURCE_DIR}/src/factory_bank.inc --check)
endif()

# Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(matrixbrute-screenshot tools/screenshot.cpp src/editor.cpp src/synth_link.cpp)
   target_include_directories(matrixbrute-screenshot PRIVATE src)
   target_link_libraries(matrixbrute-screenshot PRIVATE jclap::ui)
   if(WIN32)
      target_link_libraries(matrixbrute-screenshot PRIVATE ws2_32)
   endif()
   jclap_add_screenshot_test(matrixbrute-screenshot-test TOOL matrixbrute-screenshot REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
endif()
