# jclap_add_plugin(<name> OUTPUT_NAME <display name> BUNDLE_ID <id> SOURCES <files...>)
#
# Builds one CLAP plugin binary. Every plugin lands in ${CMAKE_BINARY_DIR}/clap
# (no per-config subfolder) so packaging can just grab clap/*.clap.
function(jclap_add_plugin name)
   cmake_parse_arguments(ARG "" "OUTPUT_NAME;BUNDLE_ID" "SOURCES" ${ARGN})

   add_library(${name} MODULE ${ARG_SOURCES})
   target_link_libraries(${name} PRIVATE clap)
   target_compile_features(${name} PRIVATE cxx_std_17)

   # $<1:...> stops multi-config generators (MSVC, Xcode) from appending /Release.
   set_target_properties(${name} PROPERTIES
      OUTPUT_NAME "${ARG_OUTPUT_NAME}"
      LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/clap>")

   if(APPLE)
      set_target_properties(${name} PROPERTIES
         BUNDLE TRUE
         BUNDLE_EXTENSION clap
         MACOSX_BUNDLE_GUI_IDENTIFIER ${ARG_BUNDLE_ID}
         MACOSX_BUNDLE_BUNDLE_NAME "${ARG_OUTPUT_NAME}"
         MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
         MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
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
