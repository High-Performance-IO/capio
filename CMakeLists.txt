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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Generate the compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Set compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0 -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Werror")

#####################################
# Options
#####################################
option(CAPIO_BUILD_TESTS "Build CAPIO test suite" FALSE)
option(CAPIO_LOG "Enable capio debug logging" FALSE)
option(ENABLE_COVERAGE "Enable code coverage collection" FALSE)
option(CAPIO_BUILD_SERVER "Build CAPIO server component" TRUE)
option(CAPIO_BUILD_POSIX  "Build CAPIO posix component" TRUE)

#####################################
# CMake module imports
#####################################
include(FetchContent)
include(GNUInstallDirs)

set(CAPIO_VENDOR_DIR "${PROJECT_SOURCE_DIR}/vendor")
if (IS_DIRECTORY "${CAPIO_VENDOR_DIR}/_deps")
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
    set(CALF_PROTOBUF_FORCE_FETCH ON CACHE BOOL "" FORCE)
    set(protobuf_FORCE_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    foreach (dependency absl args calf capio_cl jsoncons protobuf tomlplusplus)
        string(TOUPPER "${dependency}" dependency_upper)
        set(FETCHCONTENT_SOURCE_DIR_${dependency_upper} "${CAPIO_VENDOR_DIR}/_deps/${dependency}-src")
    endforeach ()
    set(CAPIO_SYSCALL_INTERCEPT_SOURCE_DIR "${CAPIO_VENDOR_DIR}/syscall_intercept" CACHE PATH "" FORCE)
endif ()

#####################################
# Dependencies
#####################################
find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

#####################################
# Definitions
#####################################
add_compile_definitions(CAPIO_VERSION="${CMAKE_PROJECT_VERSION}")

set(CAPIO_LOG_ENABLED FALSE)
IF (CAPIO_LOG AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CAPIO_LOG_ENABLED TRUE)
    message(STATUS "Enabling CAPIO logger")
    add_compile_definitions(CAPIO_LOG)
    execute_process(
            COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/gen_syscallnames.sh"
            "${PROJECT_BINARY_DIR}/include/syscall/syscallnames.h"
            ${CMAKE_CXX_COMPILER}
    )
ELSEIF (CAPIO_LOG)
    message(WARNING "CAPIO logger is only available in Debug builds; logging is disabled")
ENDIF ()

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_COMMON_HEADERS "capio/common/*.hpp")
include_directories(capio)

IF (CAPIO_LOG_ENABLED)
    include_directories("${PROJECT_BINARY_DIR}/include/syscall")
ENDIF ()


#####################################
# Global required dependencies
#####################################
FetchContent_Declare(
        capio_cl
        GIT_REPOSITORY https://github.com/High-Performance-IO/CAPIO-CL.git
        GIT_TAG v1.5.2
)

FetchContent_Declare(
        calf
        GIT_REPOSITORY https://github.com/High-Performance-IO/calf.git
        GIT_TAG v0.3.1
)

FetchContent_MakeAvailable(calf)

#####################################
# Targets
#####################################

IF(CAPIO_BUILD_POSIX)
	message(STATUS "Adding CAPIO posix component")
	add_subdirectory(capio/posix)
ENDIF()

IF(CAPIO_BUILD_SERVER)
	message(STATUS "Adding CAPIO server component")
	add_subdirectory(capio/server)
ENDIF()

IF (CAPIO_BUILD_TESTS)
    message(STATUS "Building CAPIO test suite")
    add_subdirectory(capio/tests)
ENDIF (CAPIO_BUILD_TESTS)

IF (CAPIO_BUILD_SERVER AND CAPIO_BUILD_POSIX)
    set(CAPIO_MODULE_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/dist")
    set(CAPIO_MODULE_ARCHIVE "${CAPIO_MODULE_OUTPUT_DIR}/capio-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}.tar.gz")
    set(CAPIO_MODULE_STAGE_PARENT "${PROJECT_BINARY_DIR}/module")
    set(CAPIO_MODULE_STAGE "${CAPIO_MODULE_STAGE_PARENT}/capio-${PROJECT_VERSION}")
    message(STATUS "CAPIO module bundle artifact: ${CAPIO_MODULE_ARCHIVE}")
    install(FILES "${PROJECT_SOURCE_DIR}/scripts/capio.module"
            DESTINATION modulefiles/capio
            RENAME "${PROJECT_VERSION}"
            COMPONENT capio_module
    )
    add_custom_target(module_bundle
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${CAPIO_MODULE_STAGE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CAPIO_MODULE_STAGE}" "${CAPIO_MODULE_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} --install "${PROJECT_BINARY_DIR}" --prefix "${CAPIO_MODULE_STAGE}"
                    --component capio_module --strip
            COMMAND ${CMAKE_COMMAND} -E chdir "${CAPIO_MODULE_STAGE_PARENT}"
                    ${CMAKE_COMMAND} -E tar czf "${CAPIO_MODULE_ARCHIVE}" "capio-${PROJECT_VERSION}"
            DEPENDS capio_server capio_posix
            USES_TERMINAL
            COMMENT "Building CAPIO module bundle: ${CAPIO_MODULE_ARCHIVE}"
    )

    set(CAPIO_OFFLINE_SOURCE_ARCHIVE "${CAPIO_MODULE_OUTPUT_DIR}/capio-${PROJECT_VERSION}-offline-source.tar.gz")
    message(STATUS "CAPIO offline source bundle: ${CAPIO_OFFLINE_SOURCE_ARCHIVE}")
    add_custom_target(offline_source_bundle
            COMMAND ${CMAKE_COMMAND} -E env
                    BUILD_DIR=${PROJECT_BINARY_DIR}
                    "${PROJECT_SOURCE_DIR}/scripts/build_offline_source_bundle.sh"
                    "${CAPIO_MODULE_OUTPUT_DIR}"
            USES_TERMINAL
            COMMENT "Building CAPIO offline source bundle: ${CAPIO_OFFLINE_SOURCE_ARCHIVE}"
    )
ENDIF ()
