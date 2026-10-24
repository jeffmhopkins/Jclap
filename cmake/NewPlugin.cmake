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
# DISPLAY_NAME goes into C++ string literals and CMake files, so no quotes, backslashes, semicolons
# or control characters.
if(NOT DISPLAY_NAME MATCHES "^[A-Za-z0-9 _.,:()&+'!?-]+$")
   message(FATAL_ERROR "DISPLAY_NAME may only use letters, digits, spaces and _.,:()&+'!?- (got '${DISPLAY_NAME}')")
endif()

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(src "${root}/plugins/_template")
set(dst "${root}/plugins/${NAME}")
if(EXISTS "${dst}")
   message(FATAL_ERROR "${dst} already exists")
endif()
# Two plugins with one FILE_NAME would build the same .clap and overwrite each other.
file(GLOB plugin_lists "${root}/plugins/*/CMakeLists.txt")
foreach(list_file IN LISTS plugin_lists)
   file(READ "${list_file}" other)
   string(TOLOWER "${other}" other)
   string(TOLOWER "OUTPUT_NAME \"${FILE_NAME}\"" mine)
   string(FIND "${other}" "${mine}" at)
   if(NOT at EQUAL -1 AND NOT list_file MATCHES "/_template/")
      message(FATAL_ERROR "FILE_NAME ${FILE_NAME} is already used by ${list_file}")
   endif()
endforeach()

# C++ namespace: the folder name with dashes as underscores.
string(REPLACE "-" "_" ident "${NAME}")
string(TOUPPER "${DISPLAY_NAME}" display_upper)

# The state magic: J plus the first three letters of FILE_NAME, so a new plugin's saved state can't be
# mistaken for the template's or another plugin's that started from it.
string(TOUPPER "${FILE_NAME}XXX" magic_src)
string(SUBSTRING "${magic_src}" 0 3 magic)
string(SUBSTRING "${magic}" 0 1 m1)
string(SUBSTRING "${magic}" 1 1 m2)
string(SUBSTRING "${magic}" 2 1 m3)
set(magic_line "constexpr char kStateMagic[4] = {'J', '${m1}', '${m2}', '${m3}'};")
foreach(list_file IN LISTS plugin_lists)
   get_filename_component(plugin_dir "${list_file}" DIRECTORY)
   file(GLOB sources "${plugin_dir}/src/*.cpp")
   foreach(source IN LISTS sources)
      file(READ "${source}" other)
      string(FIND "${other}" "${magic_line}" at)
      if(NOT at EQUAL -1)
         message(WARNING "${source} already uses state magic J${magic}; change kStateMagic in src/plugin.cpp.")
      endif()
   endforeach()
endforeach()

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
   string(REPLACE "constexpr char kStateMagic[4] = {'J', 'T', 'M', 'P'};" "${magic_line}" text "${text}")
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
message(STATUS "Its saved state starts with J${magic} (kStateMagic in src/plugin.cpp).")
message(STATUS "Next: write README.md, add a row to the top-level README.")
