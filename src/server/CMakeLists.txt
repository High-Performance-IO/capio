add_executable(capio_server capio_server.cpp simdjson/simdjson.cpp)

file(GLOB_RECURSE CAPIO_SERVER_HEADERS "*.hpp")
target_sources(capio_server PRIVATE "${CAPIO_COMMON_HEADERS}" "${CAPIO_SERVER_HEADERS}" simdjson/simdjson.h)

find_package(MPI REQUIRED)

if(MPI_COMPILE_FLAGS)
    set_target_properties(capio_server PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(capio_server PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

target_include_directories(capio_server PRIVATE . ${MPI_INCLUDE_PATH})
target_link_libraries(capio_server PRIVATE ${MPI_LIBRARIES} rt stdc++fs)