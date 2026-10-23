# jclap_add_plugin(<name> OUTPUT_NAME <FileName> BUNDLE_ID <id> [OUTPUT_DIR <dir>] SOURCES <files...>)
#
# OUTPUT_NAME is the .clap file name; keep it free of spaces. The name hosts display comes from
# the plugin descriptor in code.
#
# Builds one CLAP plugin binary. Every plugin lands in ${CMAKE_BINARY_DIR}/clap
# (no per-config subfolder) so packaging can just grab clap/*.clap. OUTPUT_DIR puts it somewhere
# else under the build folder instead, for plugins that shouldn't ship (the template).
function(jclap_add_plugin name)
   cmake_parse_arguments(ARG "" "OUTPUT_NAME;BUNDLE_ID;OUTPUT_DIR" "SOURCES" ${ARGN})
   if(NOT ARG_OUTPUT_DIR)
      set(ARG_OUTPUT_DIR clap)
   endif()

   add_library(${name} MODULE ${ARG_SOURCES})
   target_link_libraries(${name} PRIVATE clap)
   target_compile_features(${name} PRIVATE cxx_std_17)

   # $<1:...> stops multi-config generators (MSVC, Xcode) from appending /Release.
   set_target_properties(${name} PROPERTIES
      OUTPUT_NAME "${ARG_OUTPUT_NAME}"
      LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/${ARG_OUTPUT_DIR}>")

   if(APPLE)
      set_target_properties(${name} PROPERTIES
         BUNDLE TRUE
         BUNDLE_EXTENSION clap
         MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
         MACOSX_BUNDLE_GUI_IDENTIFIER ${ARG_BUNDLE_ID}
         MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"
         MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
         MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
      # Classic bundle type marker that some hosts and Finder still look for.
      file(WRITE ${CMAKE_BINARY_DIR}/PkgInfo "BNDL????")
      target_sources(${name} PRIVATE ${CMAKE_BINARY_DIR}/PkgInfo)
      set_source_files_properties(${CMAKE_BINARY_DIR}/PkgInfo PROPERTIES MACOSX_PACKAGE_LOCATION .)
   else()
      set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".clap")
   endif()

   if(NOT MSVC)
      target_compile_options(${name} PRIVATE -Wall -Wextra -fvisibility=hidden)
   endif()
endfunction()

# jclap_add_test(<name> SOURCES <files...>) registers a plain executable as a ctest.
function(jclap_add_test name)
   cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
   add_executable(${name} ${ARG_SOURCES})
   target_compile_features(${name} PRIVATE cxx_std_17)
   add_test(NAME ${name} COMMAND ${name})
endfunction()

# jclap_add_screenshot_test(<name> TOOL <target> REFERENCE <png>) renders with a screenshot tool
# (which takes the output path as its only argument) and checks the result against a committed
# PNG, so visual regressions fail the build. Needs JCLAP_BUILD_TOOLS and Python 3.
function(jclap_add_screenshot_test name)
   cmake_parse_arguments(ARG "" "TOOL;REFERENCE" "" ${ARGN})
   if(NOT EXISTS ${ARG_REFERENCE})
      message(STATUS "No ${ARG_REFERENCE} yet; render it with ${ARG_TOOL} to enable ${name}")
      return()
   endif()
   find_package(Python3 COMPONENTS Interpreter)
   if(NOT Python3_FOUND)
      message(STATUS "Python 3 not found; skipping screenshot test ${name}")
      return()
   endif()
   add_test(NAME ${name}
      COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:${ARG_TOOL}> -DREF=${ARG_REFERENCE}
         -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${name}.png -DPYTHON=${Python3_EXECUTABLE}
         -P ${CMAKE_SOURCE_DIR}/cmake/ScreenshotTest.cmake)
endfunction()
