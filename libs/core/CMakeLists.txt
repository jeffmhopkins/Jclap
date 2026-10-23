# jclap::core: the CLAP boilerplate every plugin needs (plugin base class, factory/entry, ports,
# param and state helpers, GUI edit queue, a tiny test host). No GUI dependencies.

add_library(jclap_core STATIC
   src/plugin.cpp
   src/edit_queue.cpp
   src/helpers.cpp)
add_library(jclap::core ALIAS jclap_core)
target_include_directories(jclap_core PUBLIC include)
target_compile_features(jclap_core PUBLIC cxx_std_17)
target_link_libraries(jclap_core PUBLIC clap)
if(NOT MSVC)
   target_compile_options(jclap_core PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

jclap_add_test(jclap-core-tests SOURCES tests/test_core.cpp)
target_link_libraries(jclap-core-tests PRIVATE jclap::core)
