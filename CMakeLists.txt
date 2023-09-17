cmake_minimum_required(VERSION 3.15)
project(capio)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Threads REQUIRED)

OPTION(DEFINE_MACRO "Option description" OFF) # Disabled by default
IF(DEFINE_MACRO)
    ADD_DEFINITIONS(-DCAPIOLOG)
ENDIF(DEFINE_MACRO)


OPTION(CAPIO_SYNC "Use sync MPI primitives" OFF) # Disabled by default
IF(CAPIO_SYNC)
	ADD_DEFINITIONS(-DCAPIOSYNC)
ENDIF(CAPIO_SYNC)

set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -pedantic -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

file(GLOB_RECURSE CAPIO_COMMON_HEADERS "src/common/capio/*.hpp")
include_directories(src/common)

add_subdirectory(src/posix)
add_subdirectory(src/server)
