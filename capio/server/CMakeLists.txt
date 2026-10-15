#####################################
# Target information
#####################################
set(TARGET_NAME capio_server)
file(GLOB_RECURSE TARGET_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp"
        "capio_server.cpp"
)
if (NOT MPI_CXX_FOUND)
    list(FILTER TARGET_SOURCES EXCLUDE REGEX "/src/mpi_backend\\.cpp$")
endif ()

#####################################
# External projects
#####################################
FetchContent_Declare(
        args
        GIT_REPOSITORY https://github.com/Taywee/args.git
        GIT_TAG 6.4.7
)

set(ARGS_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(ARGS_BUILD_UNITTESTS OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(args capio_cl mtcl)


#####################################
# CAPIOCL dependency compatibility
#####################################
if (TARGET libcapio_cl)
    target_compile_options(libcapio_cl PRIVATE $<$<NOT:$<CONFIG:Debug>>:-UCALF_LOG>)
endif ()

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.hpp")
if (NOT MPI_CXX_FOUND)
    list(FILTER CAPIO_SERVER_HEADERS EXCLUDE REGEX "/remote/backend/mpi\\.hpp$")
endif ()
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MPI_INCLUDE_PATH}
        ${capio_cl_SOURCE_DIR}
)
target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${mtcl_SOURCE_DIR}/include)

#####################################
# set CALF logger component name
#####################################
calf_enable_log(${TARGET_NAME} ${CAPIO_LOG_ENABLED})
calf_set_component(${TARGET_NAME} "server")
calf_set_default_log_dir(${TARGET_NAME} "./capio_logs")

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
target_link_libraries(${TARGET_NAME} PRIVATE pthread rt stdc++fs libcapio_cl args)
if (MPI_CXX_FOUND)
    target_compile_definitions(${TARGET_NAME} PRIVATE CAPIO_HAS_MPI)
    target_link_libraries(${TARGET_NAME} PRIVATE MPI::MPI_CXX)
endif ()

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT capio_module
)

#####################################
# Code coverage
#####################################
IF (ENABLE_COVERAGE)
    IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(${TARGET_NAME} PRIVATE CAPIO_COVERAGE)
        target_compile_options(${TARGET_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${TARGET_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        IF (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_link_libraries(${TARGET_NAME} PRIVATE gcov)
        ENDIF (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    ELSE (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Code coverage is disabled in release mode.")
    ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")
ENDIF (ENABLE_COVERAGE)
