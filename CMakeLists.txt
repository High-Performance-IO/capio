cmake_minimum_required(VERSION 3.15)
project(capio)

set(CMAKE_CXX_STANDARD 14)
find_package(MPI REQUIRED)
add_subdirectory(mpi_io_examples)