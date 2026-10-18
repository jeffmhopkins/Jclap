cmake_minimum_required(VERSION 3.17)
project(jclap-orb-polar VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(clap
   GIT_REPOSITORY https://github.com/free-audio/clap.git
   GIT_TAG 1.2.2)
FetchContent_MakeAvailable(clap)

add_library(orb-polar MODULE src/plugin.cpp)
target_link_libraries(orb-polar PRIVATE clap)
target_include_directories(orb-polar PRIVATE src)

if(APPLE)
   set_target_properties(orb-polar PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION clap
      OUTPUT_NAME "Orb Polar"
      MACOSX_BUNDLE_GUI_IDENTIFIER com.jeffmhopkins.jclap.orb-polar
      MACOSX_BUNDLE_BUNDLE_NAME "Orb Polar"
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
else()
   set_target_properties(orb-polar PROPERTIES
      PREFIX ""
      SUFFIX ".clap"
      OUTPUT_NAME "Orb Polar")
endif()

if(NOT MSVC)
   target_compile_options(orb-polar PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

enable_testing()
add_executable(test-polar tests/test_polar.cpp)
target_include_directories(test-polar PRIVATE src)
add_test(NAME polar COMMAND test-polar)
