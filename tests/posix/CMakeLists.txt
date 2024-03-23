#####################################
# Target information
#####################################
set(TARGET_NAME capio_posix_tests)
set(TARGET_INCLUDE_FOLDER "${PROJECT_SOURCE_DIR}/src/posix")
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/realpath.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# External projects
#####################################
set(SYSCALL_INTERCEPT_BINARY_DIR "${PROJECT_BINARY_DIR}/src/posix/syscall_intercept")
set(SYSCALL_INTERCEPT_INCLUDE_FOLDER
        "${SYSCALL_INTERCEPT_BINARY_DIR}/${CMAKE_INSTALL_INCLUDEDIR}")
set(SYSCALL_INTERCEPT_LIB_FOLDER
        "${SYSCALL_INTERCEPT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

#####################################
# Definitions
#####################################
target_compile_definitions(${TARGET_NAME} PRIVATE __CAPIO_POSIX)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_POSIX_HEADERS "${TARGET_INCLUDE_FOLDER}/*.hpp")
file(GLOB_RECURSE SYSCALL_INTERCEPT_HEADERS "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}/*.h")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_POSIX_HEADERS}"
        "${GTEST_CONFIG_HEADERS}"
        "${SYSCALL_INTERCEPT_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        "${TARGET_INCLUDE_FOLDER}"
        "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}"
)

#####################################
# Link libraries
#####################################
target_link_directories(${TARGET_NAME} PRIVATE ${SYSCALL_INTERCEPT_LIB_FOLDER})
target_link_libraries(${TARGET_NAME} PRIVATE rt syscall_intercept GTest::gtest_main)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
