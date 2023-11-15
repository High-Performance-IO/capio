#####################################
# Target information
#####################################
set(TARGET_NAME capio_syscall_tests)
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/clone.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/directory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dup.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rename.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/statx.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/write.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dirents.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE Catch2::Catch2WithMain)

#####################################
# Configure tests
#####################################
catch_discover_tests(${TARGET_NAME}
        PROPERTIES
        ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
