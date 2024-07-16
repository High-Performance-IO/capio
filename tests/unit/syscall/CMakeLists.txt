#####################################
# Target information
#####################################
set(TARGET_NAME capio_syscall_unit_tests)
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/clone.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/chdir.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/directory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dirent.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dup.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/fcntl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rename.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/statx.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/write.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)