#####################################
# Target information
#####################################
set(TARGET_NAME capio_validator_tests)
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

#####################################
# Expose CMAKE_INSTALL_BINDIR as var
#####################################
set(CAPIO_TEST_EXTRA_DIR "${CMAKE_INSTALL_PREFIX}/capio_tests")
add_compile_definitions(CAPIO_INSTALL_TARGET_DIR="${CAPIO_TEST_EXTRA_DIR}")


#####################################
# Fetch required libraries
#####################################
FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(simdjson)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES} ${simdjson_SOURCE_DIR}/singleheader/simdjson.cpp)


#####################################
# Expose required libraries
#####################################
target_include_directories(${TARGET_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/cl_validate/src/
        ${simdjson_SOURCE_DIR}
)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest)

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/test1.json DESTINATION ${CAPIO_TEST_EXTRA_DIR})