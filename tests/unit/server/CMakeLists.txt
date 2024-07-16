#####################################
# Target information
#####################################
set(TARGET_NAME capio_server_unit_tests)
set(TARGET_INCLUDE_FOLDER "${PROJECT_SOURCE_DIR}/src/server")
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/capio_file.cpp
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
target_include_directories(${TARGET_NAME} PRIVATE ${TARGET_INCLUDE_FOLDER})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main rt)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)