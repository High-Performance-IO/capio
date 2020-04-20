cmake_minimum_required(VERSION 3.15)
project(capio)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-lrt -pthread")
find_package(MPI REQUIRED)
add_subdirectory(mpi_io_examples)
add_subdirectory(tests)