#####################################
# Target information
#####################################
set(TARGET_NAME capio_posix)
set(TARGET_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
set(TARGET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/libcapio_posix.cpp)

#####################################
# Target definition
#####################################
add_library(${TARGET_NAME} SHARED ${TARGET_SOURCES})

#####################################
# CMake module imports
#####################################
include(ProcessorCount)

#####################################
# External projects
#####################################
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept)

set(SYSCALL_INTERCEPT_INCLUDE_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept/${CMAKE_INSTALL_INCLUDEDIR}")
set(SYSCALL_INTERCEPT_LIB_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept/${CMAKE_INSTALL_LIBDIR}")

execute_process(
        COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "${CMAKE_CURRENT_SOURCE_DIR}/syscall_intercept"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept"
)

IF (UNIX)
    ProcessorCount(NPROC)
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build
            "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept"
            --parallel "${NPROC}"
    )

    file(GLOB SYSCALL_INTERCEPT_HEADERS "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}/*.h")
    file(GLOB SYSCALL_INTERCEPT_LIBS "${SYSCALL_INTERCEPT_LIB_FOLDER}/*.a" "${SYSCALL_INTERCEPT_LIB_FOLDER}/*.so*")
    file(GLOB SYSCALL_INTERCEPT_PKGCONFIGS "${SYSCALL_INTERCEPT_LIB_FOLDER}/pkgconfig/*.pc")

    install(FILES ${SYSCALL_INTERCEPT_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES ${SYSCALL_INTERCEPT_LIBS} DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES ${SYSCALL_INTERCEPT_PKGCONFIGS} DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
ELSE (UNIX)
    message(WARNING, "The syscall_intercept library only supports Unix OS.")
ENDIF (UNIX)

#####################################
# Definitions
#####################################
target_compile_definitions(${TARGET_NAME} PRIVATE __CAPIO_POSIX)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_POSIX_HEADERS "*.hpp")
file(GLOB_RECURSE SYSCALL_INTERCEPT_HEADERS "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}/*.h")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_POSIX_HEADERS}"
        "${SYSCALL_INTERCEPT_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        "${TARGET_INCLUDE_FOLDER}"
        "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}"
)

#####################################
# Target properties
#####################################
set_target_properties(${TARGET_NAME} PROPERTIES
        VERSION "${CMAKE_PROJECT_VERSION}"
        SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
)

#####################################
# Link libraries
#####################################
target_link_directories(${TARGET_NAME} PRIVATE ${SYSCALL_INTERCEPT_LIB_FOLDER})
target_link_libraries(${TARGET_NAME} PRIVATE rt syscall_intercept)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

#####################################
# Code coverage
#####################################
IF (ENABLE_COVERAGE)
    IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${TARGET_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${TARGET_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        IF (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_link_libraries(${TARGET_NAME} PRIVATE gcov)
        ENDIF (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    ELSE (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Code coverage is disabled in release mode.")
    ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")
ENDIF (ENABLE_COVERAGE)