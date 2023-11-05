#####################################
# Target information
#####################################
set(TARGET_NAME capio_tests)
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/clone.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/directory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/rename.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/stat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/statx.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/write.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# External projects
#####################################
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE Catch2::Catch2WithMain)

#####################################
# CMake module imports
#####################################
include(CTest)
include(Catch)

#####################################
# Configure tests
#####################################
catch_discover_tests(${TARGET_NAME}
        PROPERTIES
        ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/src/posix/libcapio_posix.so
)