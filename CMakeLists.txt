cmake_minimum_required(VERSION 3.15)
project(capio)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)
find_package(Threads REQUIRED)
find_package(MPI REQUIRED)
add_library(capio_posix SHARED
    src/capio_posix.cpp
)
target_link_libraries(capio_posix rt)
add_subdirectory(src)
add_subdirectory(mpi_io_examples)
add_subdirectory(unit_tests)
