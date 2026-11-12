# This plugin's version: bump it when the plugin changes (see jclap_plugin_version in cmake/JClap.cmake).
jclap_plugin_version(0.13.2)

jclap_add_plugin(jbreath
   OUTPUT_NAME "JBreath"
   BUNDLE_ID com.jeffmhopkins.jclap.jbreath
   SOURCES src/plugin.cpp src/editor.cpp)
target_include_directories(jbreath PRIVATE src)
target_link_libraries(jbreath PRIVATE jclap::ui jclap::dsp)

# Host-style test of the whole plugin. It never opens the panel, but links it.
jclap_add_test(jbreath-tests SOURCES tests/test_plugin.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jbreath-tests PRIVATE src)
target_link_libraries(jbreath-tests PRIVATE jclap::ui jclap::dsp)

# The dice: every patch RANDOMIZE writes and MUTATE nudges plays, in tune and at a usable level.
jclap_add_test(jbreath-dice-tests SOURCES tests/test_dice.cpp src/plugin.cpp src/editor.cpp)
target_include_directories(jbreath-dice-tests PRIVATE src)
target_link_libraries(jbreath-dice-tests PRIVATE jclap::ui jclap::dsp)

# The instruments alone: tuning, loudness, articulation and stability, with no host.
jclap_add_test(jbreath-voice-tests SOURCES tests/test_voice.cpp)
target_include_directories(jbreath-voice-tests PRIVATE src)
target_link_libraries(jbreath-voice-tests PRIVATE jclap::core jclap::dsp)

# The smoke's fluid solver alone, and its thread.
jclap_add_test(jbreath-vapor-tests SOURCES tests/test_vapor.cpp)
target_include_directories(jbreath-vapor-tests PRIVATE src)
target_link_libraries(jbreath-vapor-tests PRIVATE jclap::core)

if(JCLAP_BUILD_TOOLS)
   # Offscreen render of the panel, for docs and visual checks. Needs a GPU (or a software Vulkan driver).
   add_executable(jbreath-screenshot tools/screenshot.cpp src/editor.cpp)
   target_include_directories(jbreath-screenshot PRIVATE src)
   target_link_libraries(jbreath-screenshot PRIVATE jclap::ui jclap::dsp)
   jclap_add_screenshot_test(jbreath-screenshot-test TOOL jbreath-screenshot
      REFERENCE ${CMAKE_CURRENT_SOURCE_DIR}/docs/panel.png)
   # Bakes src/tuning_table.h: jbreath-tune > src/tuning_table.h
   add_executable(jbreath-tune tools/tune.cpp)
   target_include_directories(jbreath-tune PRIVATE src)
   find_package(Threads REQUIRED)
   target_link_libraries(jbreath-tune PRIVATE Threads::Threads)
   # Measures how audibly each sound param changes every instrument (min, centre and max), through
   # the whole plugin: jbreath-knob-audit --out DIR [--wav DIR]. A developer tool; not run in CI.
   add_executable(jbreath-knob-audit tools/knob_audit.cpp src/plugin.cpp src/editor.cpp)
   target_include_directories(jbreath-knob-audit PRIVATE src)
   target_link_libraries(jbreath-knob-audit PRIVATE jclap::ui jclap::dsp Threads::Threads)
   # Plays a score through the whole plugin to a WAV, for listening: jbreath-render SCORE OUT.wav
   add_executable(jbreath-render tools/render.cpp src/plugin.cpp src/editor.cpp)
   target_include_directories(jbreath-render PRIVATE src)
   target_link_libraries(jbreath-render PRIVATE jclap::ui jclap::dsp)
endif()
