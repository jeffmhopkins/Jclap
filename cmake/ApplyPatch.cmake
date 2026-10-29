# Applies PATCH to the source tree in the working directory, once: a tree that already has it
# (a re-run of the patch step) is left alone. A tree with an older version of it (a cached checkout
# from before the patch changed) is reset to its fetched commit first. Used as a FetchContent
# PATCH_COMMAND:
#
#    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=<file> -P <this file>

execute_process(COMMAND git apply --reverse --check "${PATCH}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
if(applied EQUAL 0)
   return()
endif()
execute_process(COMMAND git apply --check "${PATCH}" RESULT_VARIABLE clean OUTPUT_QUIET ERROR_QUIET)
if(NOT clean EQUAL 0)
   execute_process(COMMAND git checkout -- . RESULT_VARIABLE reset)
   if(NOT reset EQUAL 0)
      message(FATAL_ERROR "Couldn't reset the tree to apply ${PATCH}")
   endif()
endif()
execute_process(COMMAND git apply --whitespace=nowarn "${PATCH}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
   message(FATAL_ERROR "Couldn't apply ${PATCH}")
endif()
