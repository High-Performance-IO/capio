#####################################
# Target information
#####################################
set(TARGET_NAME capio_server_unit_tests)

FetchContent_MakeAvailable(capio_cl)

set(TARGET_INCLUDE_FOLDER "${PROJECT_SOURCE_DIR}/capio/server")

file(GLOB_RECURSE SERVER_SOURCES
        "${CMAKE_SOURCE_DIR}/capio/server/**/*.cpp"
)

set(TARGET_SOURCES
        src/main.cpp
        ${SERVER_SOURCES}
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${TARGET_INCLUDE_FOLDER}/include/**/*.hpp")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        "${TARGET_INCLUDE_FOLDER}/include"
        ${capio_cl_SOURCE_DIR}
)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main rt libcapio_cl)

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