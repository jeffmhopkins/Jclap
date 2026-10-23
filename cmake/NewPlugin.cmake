# Creates plugins/<NAME> from plugins/_template and adds it to the build:
#
#    cmake -DNAME=my-plugin -DFILE_NAME=MyPlugin "-DDISPLAY_NAME=My Plugin" -P cmake/NewPlugin.cmake
#
# NAME is the folder, CMake target and the end of the plugin id (com.jeffmhopkins.jclap.<NAME>).
# FILE_NAME is the .clap file and C++ class name (letters and digits only). DISPLAY_NAME is what
# hosts show.

foreach(var NAME FILE_NAME DISPLAY_NAME)
   if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
      message(FATAL_ERROR "Usage: cmake -DNAME=my-plugin -DFILE_NAME=MyPlugin \"-DDISPLAY_NAME=My Plugin\" -P cmake/NewPlugin.cmake")
   endif()
endforeach()
if(NOT NAME MATCHES "^[a-z][a-z0-9-]*$")
   message(FATAL_ERROR "NAME must be lower case letters, digits or - (got '${NAME}')")
endif()
if(NOT FILE_NAME MATCHES "^[A-Za-z][A-Za-z0-9]*$")
   message(FATAL_ERROR "FILE_NAME must be letters and digits (got '${FILE_NAME}')")
endif()

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(src "${root}/plugins/_template")
set(dst "${root}/plugins/${NAME}")
if(EXISTS "${dst}")
   message(FATAL_ERROR "${dst} already exists")
endif()

# C++ namespace: the folder name with dashes as underscores.
string(REPLACE "-" "_" ident "${NAME}")
string(TOUPPER "${DISPLAY_NAME}" display_upper)

file(GLOB_RECURSE files RELATIVE "${src}" "${src}/*")
foreach(f IN LISTS files)
   if(f MATCHES "^docs/")
      continue()
   endif()
   file(READ "${src}/${f}" text)
   string(REPLACE "com.jeffmhopkins.jclap.plugin-template" "com.jeffmhopkins.jclap.${NAME}" text "${text}")
   if(f STREQUAL "CMakeLists.txt")
      string(REPLACE "plugin_template" "${NAME}" text "${text}")
      string(REPLACE "    OUTPUT_DIR template\n" "" text "${text}")
      string(REPLACE "   OUTPUT_DIR template\n" "" text "${text}")
   else()
      string(REPLACE "plugin_template" "${ident}" text "${text}")
   endif()
   string(REPLACE "PluginTemplate" "${FILE_NAME}" text "${text}")
   string(REPLACE "PLUGIN TEMPLATE" "${display_upper}" text "${text}")
   string(REPLACE "Plugin Template" "${DISPLAY_NAME}" text "${text}")
   file(WRITE "${dst}/${f}" "${text}")
endforeach()

set(ci "https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml")
file(WRITE "${dst}/README.md"
   "<div align=\"center\">\n\n# ${DISPLAY_NAME}\n\nOne line on what it does.\n\n"
   "[![CI](${ci}/badge.svg)](${ci}) "
   "[![CLAP: 1.2.2](https://img.shields.io/badge/CLAP-1.2.2-blue)](https://github.com/free-audio/clap)\n\n"
   "</div>\n\nHow to use it, and its parameters.\n")

# Register it next to the other plugins.
file(READ "${root}/CMakeLists.txt" top)
string(REGEX MATCHALL "add_subdirectory\\(plugins/[^)_][^)]*\\)" existing "${top}")
list(GET existing -1 last)
string(REPLACE "${last}" "${last}\nadd_subdirectory(plugins/${NAME})" top "${top}")
file(WRITE "${root}/CMakeLists.txt" "${top}")

message(STATUS "Created plugins/${NAME} and added it to CMakeLists.txt.")
message(STATUS "Next: change kStateMagic in src/plugin.cpp, write README.md, add a row to the top-level README.")
