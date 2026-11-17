#####################################
# Project-wide settings
#####################################
cmake_minimum_required(VERSION 3.15)

project(vpoline)

#####################################
# CMake module imports
#####################################
include(ExternalProject)

#####################################
# Import external project from git
#####################################
# vpoline is the syscall interception backend for riscv (see common/syscall.hpp).
# It is LD_PRELOADed at runtime and loads the CAPIO hook via the LIBVPHOOK env var,
# so the capio_posix target only needs its include dir, not a link.
# vpoline's CMakeLists has no install() rule: header is picked from vpoline-src/include
# and libvpoline.so from vpoline-build (fixed SOURCE_DIR/BINARY_DIR below).
ExternalProject_Add(vpoline
        GIT_REPOSITORY https://github.com/alpha-unito/vpoline.git
        GIT_TAG f6d8d993e1fb8803235b6972e2bc69e2799c34d9
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}
        SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/vpoline-src
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/vpoline-build
        CMAKE_ARGS
        -DBUILD_TESTS=OFF
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        INSTALL_COMMAND ""
)