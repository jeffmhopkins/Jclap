jclap_add_plugin(orb-polar
   OUTPUT_NAME "Orb Polar"
   BUNDLE_ID com.jeffmhopkins.jclap.orb-polar
   SOURCES src/plugin.cpp)
target_include_directories(orb-polar PRIVATE src)

jclap_add_test(orb-polar-tests SOURCES tests/test_polar.cpp)
target_include_directories(orb-polar-tests PRIVATE src)
