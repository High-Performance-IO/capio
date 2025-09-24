#####################################
# Target information
#####################################
set(TARGET_NAME capio_server_unit_tests)


#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_SERVER_SOURCES
        "${CMAKE_SOURCE_DIR}/capio-server/src/storage-service/*.cpp"
        "${CMAKE_SOURCE_DIR}/capio-server/src/capio-cl-engine/capio_cl_engine.cpp"
        "${CMAKE_SOURCE_DIR}/capio-server/src/client-manager/client_manager.cpp"
)

set(TARGET_INCLUDE_FOLDER "${CMAKE_SOURCE_DIR}/capio-server/")
file(GLOB_RECURSE CAPIO_SERVER_HEADERS "${TARGET_INCLUDE_FOLDER}/*.hpp")


target_sources(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CAPIO_SERVER_SOURCES}
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

