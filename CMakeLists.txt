#####################################
# Project-wide settings
#####################################
cmake_minimum_required(VERSION 3.15)

# Define project name and description
project(capio
        LANGUAGES CXX
        DESCRIPTION "Cross-Application Programmable I/O"
        VERSION 1.0.0
)

# Set required C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Generate the compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Set compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Silence warning from G++
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wno-terminate")
endif ()

# Silence warning from clang
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0 -Wno-gnu-zero-variadic-macro-arguments -Wno-exceptions")
endif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

#####################################
# Options
#####################################
option(CAPIO_BUILD_TESTS "Build CAPIO test suite" FALSE)
option(CAPIO_LOG "Enable capio debug logging" FALSE)
option(ENABLE_COVERAGE "Enable code coverage collection" FALSE)

#####################################
# CMake module imports
#####################################
include(FetchContent)
include(GNUInstallDirs)

#####################################
# Dependencies
#####################################
find_package(Threads REQUIRED)

#####################################
# Definitions
#####################################
add_compile_definitions(CAPIO_VERSION="${CMAKE_PROJECT_VERSION}")

# We check here and define only here the CAPIO_BUILD_TESTS macro
# as it is required by both src/posix and src/server targets
# Later on we check again to add the tests subproject, which cannot
# be added here for issues with include directories
IF (CAPIO_BUILD_TESTS)
    add_compile_definitions(CAPIO_BUILD_TESTS)
ENDIF (CAPIO_BUILD_TESTS)

IF (CAPIO_LOG)
    IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Enabling CAPIO logger")
        add_compile_definitions(CAPIO_LOG)
        execute_process(
                COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/gen_syscallnames.sh"
                "${PROJECT_BINARY_DIR}/include/syscall/syscallnames.h"
        )
    ELSE (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Capio logger enabled in release mode. skipping compilation of CAPIO logger")
    ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")
ENDIF (CAPIO_LOG)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_COMMON_HEADERS "capio-common/capio/*.hpp")
include_directories(${PROJECT_SOURCE_DIR}/capio-common)

IF (CAPIO_LOG AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    include_directories("${PROJECT_BINARY_DIR}/include/syscall")
ENDIF (CAPIO_LOG AND CMAKE_BUILD_TYPE STREQUAL "Debug")

#####################################
# Targets
#####################################
add_subdirectory(capio-posix)
add_subdirectory(capio-server)

#####################################
# Install capiorun
#####################################
install(
        FILES ${PROJECT_SOURCE_DIR}/capiorun/capiorun
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE
)


IF (CAPIO_BUILD_TESTS)
    message(STATUS "Building CAPIO test suite")
    add_compile_definitions(CAPIO_BUILD_TESTS)
    add_subdirectory(tests)
ENDIF (CAPIO_BUILD_TESTS)