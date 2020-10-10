add_executable(read_file read_file.cpp)
add_executable(read_queue read_queue.cpp)
add_executable(write_file write_file.cpp)
add_executable(write_queue write_queue.cpp)

target_link_libraries(read_file rt Threads::Threads)
target_link_libraries(read_queue rt Threads::Threads)
target_link_libraries(write_file rt Threads::Threads)
target_link_libraries(write_queue rt Threads::Threads)


include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(read_file PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_queue PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_file PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_queue PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(read_file PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_queue PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_file PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_queue PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()