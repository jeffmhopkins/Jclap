# Applies PATCH to the source tree in the working directory, once: a tree that already has it
# (a re-run of the patch step) is left alone. Used as a FetchContent PATCH_COMMAND:
#
#    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=<file> -P <this file>

execute_process(COMMAND git apply --reverse --check "${PATCH}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
if(applied EQUAL 0)
   return()
endif()
execute_process(COMMAND git apply --whitespace=nowarn "${PATCH}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
   message(FATAL_ERROR "Couldn't apply ${PATCH}")
endif()
