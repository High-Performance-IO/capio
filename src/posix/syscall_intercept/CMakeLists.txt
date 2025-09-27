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
        GIT_REPOSITORY https://github.com/alpha-unito/syscall_intercept.git
        GIT_TAG 7dbdf6ab9c576f96843ef2553b7efc7d15cf66b4
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}
        CMAKE_ARGS
        -DSTATIC_CAPSTONE=ON
        -DBUILD_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
