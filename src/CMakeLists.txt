add_executable(capio_server capio_server.cpp simdjson.cpp)

target_link_libraries(capio_server ${MPI_LIBRARIES} rt)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(capio_server PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(capio_server PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

target_link_libraries(capio_server stdc++fs)
