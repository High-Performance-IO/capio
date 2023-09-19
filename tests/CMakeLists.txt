#####################################
# Target information
#####################################
set(TARGET_NAME capio_tests)
set(TARGET_INCLUDE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/clone.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/directory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/rename.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/write.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/realpath.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# CMake module imports
#####################################
include(CTest)
include(Catch)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_TESTS_HEADERS "include/*.hpp")
target_sources(${TARGET_NAME} PRIVATE "${CAPIO_COMMON_HEADERS}" "${CAPIO_TESTS_HEADERS}")
target_include_directories(${TARGET_NAME} PRIVATE ${TARGET_INCLUDE_FOLDER})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE Catch2::Catch2WithMain)

#####################################
# Configure tests
#####################################
catch_discover_tests(${TARGET_NAME})