#####################################
# Target information
#####################################
set(TARGET_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
# The classes of the client: every .cpp file in src/, also for the unit tests
file(GLOB CAPIO_POSIX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(TARGET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/libcapio_posix.cpp ${CAPIO_POSIX_SOURCES})

#####################################
# CMake module imports
#####################################
include(ProcessorCount)

#####################################
# Interception backend selection.
# Mirrors the #if defined(__riscv) branch in common/syscall.hpp and
# libcapio_posix.cpp:
#   riscv          -> vpoline, preloaded at runtime as LD_PRELOAD=libvpoline.so and
#                     loading the hook via LIBVPHOOK (no link time dependency)
#   x86_64/aarch64 -> syscall_intercept, built and linked upstream
#####################################
if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv")
    set(CAPIO_USE_VPOLINE TRUE)
    set(BACKEND_PROJECT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/vpoline")
else ()
    set(CAPIO_USE_SYSCALL_INTERCEPT TRUE)
    set(BACKEND_PROJECT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/syscall_intercept")
endif ()

get_filename_component(BACKEND_NAME "${BACKEND_PROJECT_FOLDER}" NAME)
set(BACKEND_BUILD_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/${BACKEND_NAME}")

if (CAPIO_USE_SYSCALL_INTERCEPT)
    set(BACKEND_INCLUDE_FOLDER "${BACKEND_BUILD_FOLDER}/${CMAKE_INSTALL_INCLUDEDIR}")
    set(BACKEND_LIB_FOLDER "${BACKEND_BUILD_FOLDER}/${CMAKE_INSTALL_LIBDIR}")
else ()
    # vpoline has no install() rule: header from the cloned source tree, .so from the
    # ExternalProject build dir (paths fixed in vpoline/CMakeLists.txt)
    set(BACKEND_INCLUDE_FOLDER "${BACKEND_BUILD_FOLDER}/vpoline-src/include")
    set(BACKEND_LIB_FOLDER "${BACKEND_BUILD_FOLDER}/vpoline-build")
endif ()

#####################################
# External projects (built once, shared by both variants)
#####################################
file(MAKE_DIRECTORY ${BACKEND_BUILD_FOLDER})

execute_process(
        COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "${BACKEND_PROJECT_FOLDER}"
        WORKING_DIRECTORY "${BACKEND_BUILD_FOLDER}"
)

IF (UNIX)
    ProcessorCount(NPROC)
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build
            "${BACKEND_BUILD_FOLDER}"
            --parallel "${NPROC}"
    )

    if (CAPIO_USE_SYSCALL_INTERCEPT)
        file(GLOB BACKEND_HEADERS "${BACKEND_INCLUDE_FOLDER}/*.h")
        file(GLOB BACKEND_LIBS "${BACKEND_LIB_FOLDER}/*.a" "${BACKEND_LIB_FOLDER}/*.so*")
        file(GLOB BACKEND_PKGCONFIGS "${BACKEND_LIB_FOLDER}/pkgconfig/*.pc")

        install(FILES ${BACKEND_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
        install(FILES ${BACKEND_LIBS} DESTINATION "${CMAKE_INSTALL_LIBDIR}")
        install(FILES ${BACKEND_PKGCONFIGS} DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    else ()
        # Ship libvpoline.so so the riscv launch can use an installed copy.
        file(GLOB BACKEND_LIBS "${BACKEND_LIB_FOLDER}/libvpoline.so*")
        install(FILES ${BACKEND_LIBS} DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif ()
ELSE (UNIX)
    message(WARNING, "The syscall interception backend only supports Unix OS.")
ENDIF (UNIX)

#####################################
# Headers (collected once)
#####################################
file(GLOB_RECURSE CAPIO_POSIX_HEADERS "*.hpp")

#####################################
# Variant definition
#   capio_posix_add_variant(<target name> <mem stream: 1|0>)
#####################################
function(capio_posix_add_variant TARGET_NAME MEM_STREAM)
    add_library(${TARGET_NAME} SHARED ${TARGET_SOURCES})

    # Definitions
    target_compile_definitions(${TARGET_NAME} PRIVATE
            __CAPIO_POSIX
            CAPIO_MEM_STREAM=${MEM_STREAM}
    )

    # The syscall hook may access thread-local state while resolving TLS itself.
    # LD_PRELOAD libraries therefore require direct, loader-allocated TLS access.
    target_compile_options(${TARGET_NAME} PRIVATE -ftls-model=initial-exec)

    # Include files and directories
    target_sources(${TARGET_NAME} PRIVATE
            "${CAPIO_COMMON_HEADERS}"
            "${CAPIO_POSIX_HEADERS}"
            "${BACKEND_HEADERS}"
    )
    target_include_directories(${TARGET_NAME} PRIVATE
            "${TARGET_INCLUDE_FOLDER}"
            "${BACKEND_INCLUDE_FOLDER}"
    )

    # CALF logger component name
    calf_enable_log(${TARGET_NAME} ${CAPIO_LOG_ENABLED})
    calf_set_component(${TARGET_NAME} "posix")
    calf_set_default_log_dir(${TARGET_NAME} "./capio_logs")

    # Target properties
    set_target_properties(${TARGET_NAME} PROPERTIES
            VERSION "${CMAKE_PROJECT_VERSION}"
            SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
    )

    # Link libraries
    target_link_directories(${TARGET_NAME} PRIVATE ${BACKEND_LIB_FOLDER})
    target_link_libraries(${TARGET_NAME} PRIVATE rt)
    if (CAPIO_USE_SYSCALL_INTERCEPT)
        target_link_libraries(${TARGET_NAME} PRIVATE syscall_intercept)
    endif ()

    # Install rules
    install(TARGETS ${TARGET_NAME}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Code coverage
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
endfunction()

#####################################
# Targets
#####################################
capio_posix_add_variant(capio_posix    1)   # libcapio_posix.so    -> memory calls
capio_posix_add_variant(capio_posix_fs 0)   # libcapio_posix_fs.so -> file-system calls