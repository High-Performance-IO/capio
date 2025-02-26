#####################################
# Target information
#####################################
set(TARGET_NAME capio_backend_unit_tests)

set(TARGET_INCLUDE_FOLDER "../../../src/server")
set(TARGET_SOURCES
        src/main.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################

# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${TARGET_INCLUDE_FOLDER}/*.hpp")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        ${TARGET_INCLUDE_FOLDER}
        ${mtcl_SOURCE_DIR}/include
)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main rt)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ../../..
)


#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)

