#####################################
# Target information
#####################################
set(TARGET_NAME capio_server_unit_tests)

FetchContent_MakeAvailable(capio_cl mtcl)

set(TARGET_INCLUDE_FOLDER "${PROJECT_SOURCE_DIR}/capio/server")

file(GLOB_RECURSE SERVER_SOURCES
        "${CMAKE_SOURCE_DIR}/capio/server/**/*.cpp"
)
if (NOT MPI_CXX_FOUND)
    list(FILTER SERVER_SOURCES EXCLUDE REGEX "/src/mpi_backend\\.cpp$")
endif ()

file(GLOB_RECURSE TARGET_TEST_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp"
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_TEST_SOURCE} ${SERVER_SOURCES} )

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${TARGET_INCLUDE_FOLDER}/include/**/*.hpp")
if (NOT MPI_CXX_FOUND)
    list(FILTER CAPIO_SERVER_HEADERS EXCLUDE REGEX "/remote/backend/mpi\\.hpp$")
endif ()
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        "${TARGET_INCLUDE_FOLDER}/include"
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
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main rt libcapio_cl args)
if (MPI_CXX_FOUND)
    target_compile_definitions(${TARGET_NAME} PRIVATE CAPIO_HAS_MPI)
    target_link_libraries(${TARGET_NAME} PRIVATE MPI::MPI_CXX)
endif ()

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
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

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
