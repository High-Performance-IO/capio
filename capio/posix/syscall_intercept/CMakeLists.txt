cmake_minimum_required(VERSION 3.15)
project(syscall_intercept_setup)

include(ExternalProject)

# Define a local install directory for external dependencies
set(EXT_INSTALL_DIR "${CMAKE_BINARY_DIR}/external_install")
set(CAPSTONE_PATH "${EXT_INSTALL_DIR}")

#####################################
# 1. Capstone External Project
#####################################
ExternalProject_Add(capstone_project
        GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
        GIT_TAG 6.0.0-Alpha5
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/capstone
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EXT_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=Release
        -DCAPSTONE_BUILD_TESTS=OFF
        -DCAPSTONE_BUILD_C99=ON
        -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF
        -DCAPSTONE_RISCV_SUPPORT=ON
)

#####################################
# 2. Syscall Intercept External Project
#####################################
ExternalProject_Add(syscall_intercept
        GIT_REPOSITORY https://github.com/GekkoFS/syscall_intercept.git
        GIT_TAG riscv
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/

        # This ensures capstone finishes installing before this starts configuring
        DEPENDS capstone_project

        CMAKE_ARGS
        -DSTATIC_CAPSTONE=ON
        -DBUILD_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DPERFORM_STYLE_CHECKS=OFF

        # Point to our fresh Capstone build
        -DCMAKE_PREFIX_PATH=${EXT_INSTALL_DIR}
        -DPKG_CONFIG_PATH=${EXT_INSTALL_DIR}/lib/pkgconfig
        -Dcapstone_dir=${EXT_INSTALL_DIR}

        # Brute force paths for the compiler/linker
        -DCMAKE_C_FLAGS=-I${EXT_INSTALL_DIR}/include/
        -DCMAKE_SHARED_LINKER_FLAGS=-L${EXT_INSTALL_DIR}/lib
)
