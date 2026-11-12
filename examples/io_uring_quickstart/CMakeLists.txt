cmake_minimum_required(VERSION 3.15)
project(capio_io_uring_quickstart CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing)

add_executable(producer producer.cpp)
target_link_libraries(producer PRIVATE PkgConfig::liburing)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE PkgConfig::liburing)
