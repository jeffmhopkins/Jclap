# Fetches the VST3 SDK and clap-wrapper, for jclap_add_vst3 (in JClap.cmake).
#
# The VST3 SDK is MIT licensed since 3.8.0; only the parts the wrapper builds against are fetched.
# patches/clap-wrapper.patch (marked "jclap:") makes the wrapper pass the plugin's MIDI out to the
# host. Without it, CCs a plugin sends vanish in a VST3 host, and Polar XY sends nothing else.

FetchContent_Declare(vst3sdk
   GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
   GIT_TAG v3.8.0_build_66
   GIT_SHALLOW TRUE
   GIT_SUBMODULES base public.sdk pluginterfaces cmake
   # Only the sources: a SOURCE_SUBDIR with no CMakeLists.txt keeps the SDK's own project (its
   # examples and VSTGUI) out of the build.
   SOURCE_SUBDIR sources-only)
FetchContent_MakeAvailable(vst3sdk)
set(VST3_SDK_ROOT ${vst3sdk_SOURCE_DIR} CACHE PATH "VST3 SDK for clap-wrapper" FORCE)

# clap-wrapper links its pieces statically to the MSVC runtime unless told otherwise; match ours.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
FetchContent_Declare(clap-wrapper
   GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
   GIT_TAG v0.17.0
   GIT_SHALLOW TRUE
   PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/clap-wrapper.patch
                 -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake)
FetchContent_MakeAvailable(clap-wrapper)

# The SDK's hosting code, for tests that load a built .vst3 the way a host does.
set(_hosting ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting)
add_library(jclap_vst3_hosting STATIC
   ${_hosting}/module.cpp
   ${_hosting}/hostclasses.cpp
   ${_hosting}/pluginterfacesupport.cpp
   ${_hosting}/eventlist.cpp
   ${_hosting}/parameterchanges.cpp
   ${_hosting}/processdata.cpp)
if(APPLE)
   target_sources(jclap_vst3_hosting PRIVATE ${_hosting}/module_mac.mm)
   set_source_files_properties(${_hosting}/module_mac.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
   target_link_libraries(jclap_vst3_hosting PUBLIC "-framework Foundation" "-framework CoreFoundation")
elseif(WIN32)
   target_sources(jclap_vst3_hosting PRIVATE ${_hosting}/module_win32.cpp)
else()
   target_sources(jclap_vst3_hosting PRIVATE ${_hosting}/module_linux.cpp)
   target_link_libraries(jclap_vst3_hosting PUBLIC ${CMAKE_DL_LIBS})
endif()
target_link_libraries(jclap_vst3_hosting PUBLIC base-sdk-vst3)
