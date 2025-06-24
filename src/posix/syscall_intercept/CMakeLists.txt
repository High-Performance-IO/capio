#####################################
# Project-wide settings
#####################################
cmake_minimum_required(VERSION 3.15)

project(syscall_intercept)

#####################################
# CMake module imports
#####################################
include(ExternalProject)

#####################################
# Import external project from git
#####################################
ExternalProject_Add(syscall_intercept
        GIT_REPOSITORY https://github.com/alpha-unito/syscall_intercept
        GIT_TAG 623aa8415b2ff0b9b81bd08f791850723edea18c
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}
        CMAKE_ARGS
        -DBUILD_TESTS=OFF
        -DSTATIC_CAPSTONE=ON
        -DBUILD_EXAMPLES=OFF
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)