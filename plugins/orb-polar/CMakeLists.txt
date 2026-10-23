jclap_add_plugin(orb-polar
   OUTPUT_NAME "PolarXY"
   BUNDLE_ID com.jeffmhopkins.jclap.polar-xy
   SOURCES src/plugin.cpp)
target_include_directories(orb-polar PRIVATE src)
target_link_libraries(orb-polar PRIVATE jclap::core)

jclap_add_test(orb-polar-tests SOURCES tests/test_polar.cpp)
target_include_directories(orb-polar-tests PRIVATE src)

# Host-style test of the whole plugin.
jclap_add_test(orb-polar-plugin-tests SOURCES tests/test_plugin.cpp src/plugin.cpp)
target_include_directories(orb-polar-plugin-tests PRIVATE src)
target_link_libraries(orb-polar-plugin-tests PRIVATE jclap::core)
