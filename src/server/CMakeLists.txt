#####################################
# Target information
#####################################
set(TARGET_NAME capio_server)
set(TARGET_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/capio_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/simdjson/simdjson.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# CMake module imports
#####################################
include(FetchContent)

#####################################
# External projects
#####################################
FetchContent_Declare(
        args
        GIT_REPOSITORY https://github.com/Taywee/args.git
        GIT_TAG 6.4.6
)
FetchContent_MakeAvailable(args)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "*.hpp")
target_sources(${TARGET_NAME} PRIVATE "${CAPIO_COMMON_HEADERS}" "${CAPIO_SERVER_HEADERS}" simdjson/simdjson.h)
target_include_directories(${TARGET_NAME} PRIVATE . ${MPI_INCLUDE_PATH} ${args_SOURCE_DIR})

#####################################
# Target properties
#####################################
IF(MPI_COMPILE_FLAGS)
    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
ENDIF (MPI_COMPILE_FLAGS)

IF(MPI_LINK_FLAGS)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "${MPI_LINK_FLAGS}")
ENDIF(MPI_LINK_FLAGS)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE ${MPI_LIBRARIES} rt stdc++fs)
