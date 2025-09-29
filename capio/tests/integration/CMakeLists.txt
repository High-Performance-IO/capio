#####################################
# Target information
#####################################
set(TARGET_NAME capio_integration_tests)
set(TARGET_SOURCES
        src/main.cpp
        src/mapreduce.cpp
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
        WORKING_DIRECTORY ../../..
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)