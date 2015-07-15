cmake_minimum_required(VERSION 2.8)

#set(CMAKE_C_COMPILER clang)
#plugin source files
set(PLUGIN_SOURCE perfc.c)

#additional c flags
set(CMAKE_C_FLAGS "-D_GNU_SOURCE -std=c11")

#debugging c flags
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DHAVE_DEBUG -O0 -Wstrict-prototypes -Wall -Wundef -Wno-long-long -Wsign-compare -Wcomment -pedantic -finline-functions -fno-strict-aliasing")
set(CMAKE_C_FLAGS_RELEASE "-Os")

include(common/FindVampirTrace.cmake)
if(VT_FOUND)
    add_library(PerfCVT SHARED ${PLUGIN_SOURCE})
    set_target_properties(PerfCVT PROPERTIES COMPILE_DEFINITIONS VT)
    set_target_properties(PerfCVT PROPERTIES INCLUDE_DIRECTORIES "${VT_INCLUDE_DIRS}")
    target_link_libraries(PerfCVT pthread m)
endif()

include(common/FindScorep.cmake)
if(SCOREP_FOUND)
    add_library(PerfC SHARED ${PLUGIN_SOURCE})
    set_target_properties(PerfC PROPERTIES COMPILE_DEFINITIONS VT)
    set_target_properties(PerfC PROPERTIES INCLUDE_DIRECTORIES "${SCOREP_INCLUDE_DIRS}")
    target_link_libraries(PerfC pthread m)
endif()
    
if(NOT SCOREP_FOUND AND NOT VT_FOUND)
    message(FATAL_ERROR "Found neither ScoreP or VampirTrace backend!")
endif()
