#####################################
# Target information
#####################################
set(TARGET_NAME_MAP capio_integration_test_map)
set(TARGET_NAME_MERGE capio_integration_test_merge)
set(TARGET_NAME_SPLIT capio_integration_test_split)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME_MAP} src/mapreduce.cpp)
add_executable(${TARGET_NAME_MERGE} src/merge.cpp)
add_executable(${TARGET_NAME_SPLIT} src/split.cpp)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME_MAP} PRIVATE GTest::gtest)
target_link_libraries(${TARGET_NAME_MERGE} PRIVATE GTest::gtest)
target_link_libraries(${TARGET_NAME_SPLIT} PRIVATE GTest::gtest)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME_MAP}
        WORKING_DIRECTORY ../../..
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

gtest_discover_tests(${TARGET_NAME_MERGE}
        WORKING_DIRECTORY ../../..
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

gtest_discover_tests(${TARGET_NAME_SPLIT}
        WORKING_DIRECTORY ../../..
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME_MAP}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS ${TARGET_NAME_MERGE}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS ${TARGET_NAME_SPLIT}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)