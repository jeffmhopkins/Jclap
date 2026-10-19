cmake_minimum_required(VERSION 3.17)

# Without this, macOS binaries require the build machine's OS version (e.g. a macOS 26 CI runner
# produces plugins that refuse to load on macOS 15). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(jclap VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(clap
   GIT_REPOSITORY https://github.com/free-audio/clap.git
   GIT_TAG 1.2.2)
FetchContent_MakeAvailable(clap)

include(cmake/JClap.cmake)
enable_testing()

# One folder per plugin under plugins/, each with its own CMakeLists.txt.
add_subdirectory(plugins/orb-polar)
