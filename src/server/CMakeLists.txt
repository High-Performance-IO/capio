#####################################
# Target information
#####################################
set(TARGET_NAME capio_server)
set(TARGET_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/capio_server.cpp
        remote/backend/fs.hpp
)

#####################################
# External projects
#####################################
FetchContent_Declare(
        args
        GIT_REPOSITORY https://github.com/Taywee/args.git
        GIT_TAG 6.4.6
)
FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(args simdjson)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES} ${simdjson_SOURCE_DIR}/singleheader/simdjson.cpp)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "*.hpp")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        ${TARGET_INCLUDE_FOLDER}
        ${MPI_INCLUDE_PATH}
        ${args_SOURCE_DIR}
        ${simdjson_SOURCE_DIR}
)

#####################################
# Target properties
#####################################
IF (MPI_COMPILE_FLAGS)
    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
ENDIF (MPI_COMPILE_FLAGS)

IF (MPI_LINK_FLAGS)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "${MPI_LINK_FLAGS}")
ENDIF (MPI_LINK_FLAGS)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE ${MPI_LIBRARIES} pthread rt stdc++fs)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)