cmake_minimum_required(VERSION 2.8)
project(perfcomponent_plugin)

option(BACKEND_SCOREP "Build plugin using scorep(ON) or vampirtrace(OFF)" ON)

set(SCOREP_FOUND false)

set(PLUGIN_SOURCE perfc.c)


if(BACKEND_SCOREP)
    include(common/FindScorep.cmake)
    if(SCOREP_FOUND)
        include_directories(${SCOREP_INCLUDE_DIRS})
        add_definitions("-DSCOREP")
    else()
        message("Score-P was not found, falling back to VampirTrace!")
    endif()
endif()


if(NOT SCOREP_FOUND OR NOT BACKEND_SCOREP)
    include(common/FindVampirTrace.cmake)
    if(VT_FOUND)
        include_directories(${VT_INCLUDE_DIRS})
        add_definitions("-DVT")
    else()
        message(SEND_ERROR "Found neither Score-P nor VampirTrace backend!")
    endif()
endif()


#additional c flags
set(CMAKE_C_FLAGS "-D_GNU_SOURCE -std=c11")
#debugging c flags
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DHAVE_DEBUG -O0 -Wstrict-prototypes -Wall -Wundef -Wno-long-long -Wsign-compare -Wcomment -pedantic -finline-functions -fno-strict-aliasing")
#release c flags
set(CMAKE_C_FLAGS_RELEASE "-Os")

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SOURCE})
target_link_libraries(${PROJECT_NAME} pthread m)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
