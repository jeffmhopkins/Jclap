cmake_minimum_required(VERSION 3.17)

# Without this, macOS binaries require the build machine's OS version (e.g. a macOS 26 CI runner
# produces plugins that refuse to load on macOS 15). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(jclap VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Plugins are shared libraries, so static dependencies (Visage, bgfx) must be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(clap
   GIT_REPOSITORY https://github.com/free-audio/clap.git
   GIT_TAG 1.2.2)
FetchContent_MakeAvailable(clap)

include(cmake/JClap.cmake)
enable_testing()

# Offscreen renderers (panel screenshots, the widget gallery). They need a GPU or a software Vulkan driver.
option(JCLAP_BUILD_TOOLS "Build developer tools" OFF)

# Shared libraries plugins can link: jclap::core is the CLAP boilerplate, jclap::ui the Visage widget
# kit and CLAP window glue.
add_subdirectory(libs/core)
add_subdirectory(libs/ui)
add_subdirectory(libs/dsp)

# One folder per plugin under plugins/, each with its own CMakeLists.txt.
add_subdirectory(plugins/polar-xy)
add_subdirectory(plugins/micromonsta2)
add_subdirectory(plugins/basic-synth)
add_subdirectory(plugins/jverb)
add_subdirectory(plugins/jsynth)

# The starting point for new plugins (cmake/NewPlugin.cmake copies it). Built into build/template,
# not build/clap, so it is checked in CI but never shipped.
option(JCLAP_BUILD_TEMPLATE "Build and test the plugin template" OFF)
if(JCLAP_BUILD_TEMPLATE)
   add_subdirectory(plugins/_template)
endif()
