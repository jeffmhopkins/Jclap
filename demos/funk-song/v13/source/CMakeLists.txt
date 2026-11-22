add_compile_definitions(JCLAP_PLUGIN_VERSION="demo")
add_executable(jdrummer-funk jdrummer-funk.cpp ../plugins/jdrummer/src/plugin.cpp ../plugins/jdrummer/src/editor.cpp)
target_compile_features(jdrummer-funk PRIVATE cxx_std_17)
target_include_directories(jdrummer-funk PRIVATE ../plugins/jdrummer/src)
target_link_libraries(jdrummer-funk PRIVATE jclap::ui)

add_executable(jkit-funk jkit-funk.cpp ../plugins/jkit/src/plugin.cpp ../plugins/jkit/src/editor.cpp)
target_compile_features(jkit-funk PRIVATE cxx_std_17)
target_include_directories(jkit-funk PRIVATE ../plugins/jkit/src)
target_link_libraries(jkit-funk PRIVATE jkit_gen jclap::ui jclap::dsp)

add_executable(jarp-bass jarp-bass.cpp)
target_compile_features(jarp-bass PRIVATE cxx_std_17)
target_include_directories(jarp-bass PRIVATE ../plugins/jarp/src)
target_link_libraries(jarp-bass PRIVATE jclap::ui)

add_executable(jsynth-notes jsynth-notes.cpp)
target_compile_features(jsynth-notes PRIVATE cxx_std_17)
target_include_directories(jsynth-notes PRIVATE ../plugins/jsynth/src)
target_link_libraries(jsynth-notes PRIVATE jclap::ui jclap::dsp)

# JDrummer from another checkout (a PR branch), to preview it.
if(DEFINED JD_SRC)
   add_executable(jdrummer-funk-pr jdrummer-funk.cpp ${JD_SRC}/plugin.cpp ${JD_SRC}/editor.cpp)
   target_compile_features(jdrummer-funk-pr PRIVATE cxx_std_17)
   target_include_directories(jdrummer-funk-pr PRIVATE ${JD_SRC})
   target_link_libraries(jdrummer-funk-pr PRIVATE jclap::ui)
endif()

add_executable(jbreath-lead jbreath-lead.cpp ../plugins/jbreath/src/plugin.cpp ../plugins/jbreath/src/editor.cpp)
target_compile_features(jbreath-lead PRIVATE cxx_std_17)
target_include_directories(jbreath-lead PRIVATE ../plugins/jbreath/src)
target_link_libraries(jbreath-lead PRIVATE jclap::ui jclap::dsp)
