# jclap::dsp: header-only audio building blocks (oscillator, filter, envelope, VCA, smoother). No
# dependencies beyond the standard library; link it and include <jclap/dsp/...>.

add_library(jclap_dsp INTERFACE)
add_library(jclap::dsp ALIAS jclap_dsp)
target_include_directories(jclap_dsp INTERFACE include)
target_compile_features(jclap_dsp INTERFACE cxx_std_17)

jclap_add_test(jclap-dsp-tests SOURCES tests/test_dsp.cpp)
target_link_libraries(jclap-dsp-tests PRIVATE jclap::dsp jclap::core)
if(NOT MSVC)
   target_compile_options(jclap-dsp-tests PRIVATE -Wall -Wextra)
endif()
