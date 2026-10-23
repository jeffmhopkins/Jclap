# jclap::ui: shared Visage widgets and CLAP window glue for plugins that have a GUI.
# Link it with target_link_libraries(<plugin> PRIVATE jclap::ui).

# Visage has no releases yet, so it is pinned to a commit.
FetchContent_Declare(visage
   GIT_REPOSITORY https://github.com/VitalAudio/visage.git
   GIT_TAG 7654e32a84f95aacf9d15390272a47d2952d9565)
FetchContent_MakeAvailable(visage)

add_library(jclap_ui STATIC
   src/theme.cpp
   src/value_control.cpp
   src/knob.cpp
   src/slider.cpp
   src/button.cpp
   src/section.cpp
   src/dropdown.cpp
   src/xy_pad.cpp
   src/meter.cpp
   src/design_frame.cpp
   src/params.cpp
   src/param_bindings.cpp
   src/window.cpp
   src/clap_gui.cpp)
add_library(jclap::ui ALIAS jclap_ui)
target_include_directories(jclap_ui PUBLIC include)
target_compile_features(jclap_ui PUBLIC cxx_std_17)
target_link_libraries(jclap_ui PUBLIC jclap::core visage PRIVATE VisageEmbeddedFonts)
if(NOT MSVC)
   target_compile_options(jclap_ui PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

# Offscreen render of every widget, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
if(JCLAP_BUILD_TOOLS)
   add_executable(jclap-ui-gallery tools/gallery.cpp)
   target_link_libraries(jclap-ui-gallery PRIVATE jclap::ui)
   jclap_add_screenshot_test(jclap-ui-gallery-screenshot TOOL jclap-ui-gallery REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/gallery.png)
endif()
