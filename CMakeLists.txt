#####################################
# Project-wide settings
#####################################
cmake_minimum_required(VERSION 3.15)

# Define project name and description
project(capio LANGUAGES CXX DESCRIPTION "Cross-Application Programmable I/O")

# Set project version
set(CMAKE_PROJECT_VERSION_MAJOR 1)
set(CMAKE_PROJECT_VERSION_MINOR 0)
set(CMAKE_PROJECT_VERSION_PATCH 0)
set(CMAKE_PROJECT_VERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH})

# Set required C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Generate the compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Set compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

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
find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

#####################################
# Definitions
#####################################
add_compile_definitions(CAPIO_VERSION="${CMAKE_PROJECT_VERSION}")

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
file(GLOB_RECURSE CAPIO_COMMON_HEADERS "src/common/capio/*.hpp")
include_directories(src/common)

IF (CAPIO_LOG AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    include_directories("${PROJECT_BINARY_DIR}/include/syscall")
ENDIF (CAPIO_LOG AND CMAKE_BUILD_TYPE STREQUAL "Debug")

#####################################
# Targets
#####################################
add_subdirectory(src/posix)
add_subdirectory(src/server)

IF (CAPIO_BUILD_TESTS)
    message(STATUS "Building CAPIO test suite")
    add_subdirectory(tests)
ENDIF (CAPIO_BUILD_TESTS)
