cmake_minimum_required(VERSION 3.15)
project(syscall_intercept_setup)

include(ExternalProject)

# Define a local install directory for external dependencies
set(EXT_INSTALL_DIR "${CMAKE_BINARY_DIR}/external_install")
set(CAPSTONE_PATH "${EXT_INSTALL_DIR}")

# Common RISC-V flags to avoid repetition
set(RISCV_FLAGS "-march=rv64gc -mabi=lp64d")

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
        -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF
        -DCAPSTONE_RISCV_SUPPORT=ON
        # CRITICAL: Capstone must also use the cross-compiler
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCMAKE_C_FLAGS=${RISCV_FLAGS}
)

#####################################
# 2. Syscall Intercept External Project
#####################################
ExternalProject_Add(syscall_intercept
        GIT_REPOSITORY https://github.com/slate5/syscall_intercept.git
        GIT_TAG riscv
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept
        DEPENDS capstone_project

        CMAKE_ARGS
        -DSTATIC_CAPSTONE=ON
        -DBUILD_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DPERFORM_STYLE_CHECKS=OFF

        # 1. PROPAGATE CROSS-COMPILER
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_ASM_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}

        # 2. PASS ARCH/ABI FLAGS (Wrapped in quotes to handle spaces properly)
        -DCMAKE_C_FLAGS=${RISCV_FLAGS}\ -I${EXT_INSTALL_DIR}/include/capstone
        -DCMAKE_ASM_FLAGS=${RISCV_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=-L${EXT_INSTALL_DIR}/lib\ ${RISCV_FLAGS}

        # 3. PATHS
        -DCMAKE_PREFIX_PATH=${EXT_INSTALL_DIR}
        -DPKG_CONFIG_PATH=${EXT_INSTALL_DIR}/lib/pkgconfig
        -Dcapstone_dir=${EXT_INSTALL_DIR}
)