# Run by ctest (see jclap_add_screenshot_test): renders with TOOL to OUT (passing ARGS after it),
# then compares OUT with REF.
execute_process(COMMAND "${TOOL}" "${OUT}" ${ARGS} RESULT_VARIABLE rendered)
if(NOT rendered EQUAL 0)
   message(FATAL_ERROR "${TOOL} failed (${rendered})")
endif()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare_png.py" "${OUT}" "${REF}" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
   message(FATAL_ERROR "${OUT} doesn't match ${REF}")
endif()
