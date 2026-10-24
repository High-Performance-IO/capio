#####################################
# Target information
#####################################
set(TARGET_NAME capio_memory_allocator_benchmark)

#####################################
# Target definition
#####################################
# The benchmark is its own executable and is NOT globbed into the capio_server/unit-test targets:
# it lives outside capio/server whose sources are gathered by file(GLOB_RECURSE **/*.cpp).
# Only the buffer implementation is compiled in, so a benchmark build does not need the
# server, posix or test dependencies.
add_executable(${TARGET_NAME}
        memory_allocator.cpp
        "${PROJECT_SOURCE_DIR}/capio/server/src/storage_allocator.cpp"
)

#####################################
# Determinism
#####################################
# Benchmark always with memory checks off so sizes are not capped by host/cgroup state: results
# are reproducible across CI runners and the test never fails because of machine memory.
target_compile_definitions(${TARGET_NAME} PRIVATE CAPIO_MEMORY_CHECKS_NONE)

#####################################
# Include files and directories
#####################################
# calf/StlLogger.h - START_LOG/LOG, compiled out when logging is off (calf_enable_log not called).
target_include_directories(${TARGET_NAME} PRIVATE ${calf_SOURCE_DIR})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE pthread rt stdc++fs)

#####################################
# Configure tests
#####################################
# The root project does not call enable_testing(); CI runs test binaries directly. Registering
# here still works for any consuming build that enables CTest (BUILD_TESTING).
if(BUILD_TESTING)
    add_test(NAME memory_allocator_benchmark COMMAND ${TARGET_NAME})
endif()

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)