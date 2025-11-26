#####################################
# Target information
#####################################
set(TARGET_NAME capio_backend_unit_tests)

set(TARGET_SOURCES
        src/main.cpp
)

set(TARGET_INCLUDE_FOLDER "${CMAKE_SOURCE_DIR}/capio-server/")

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Include files and directories
#####################################
target_include_directories(${TARGET_NAME} PRIVATE
        ${TARGET_INCLUDE_FOLDER}
        ${mtcl_SOURCE_DIR}/include
        ${httplib_SOURCE_DIR}
        "${CMAKE_SOURCE_DIR}/capio-server/"
        ${capio_cl_SOURCE_DIR}
        ${capio_cl_SOURCE_DIR}/include
)

file(GLOB_RECURSE CAPIO_SERVER_SOURCES
        "${CMAKE_SOURCE_DIR}/capio-server/*.cpp"
)


file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${TARGET_INCLUDE_FOLDER}/*.hpp")

list(FILTER CAPIO_SERVER_SOURCES EXCLUDE REGEX
        ".*/json_parser\\.cpp$"
)

list(FILTER CAPIO_SERVER_SOURCES EXCLUDE REGEX
        ".*/capio_server\\.cpp$"
)

list(FILTER CAPIO_SERVER_SOURCES EXCLUDE REGEX
        ".*/command_line_parser\\.cpp$"
)


target_sources(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CAPIO_SERVER_SOURCES}
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_SERVER_HEADERS}"
)


#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main rt libcapio_cl)

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

