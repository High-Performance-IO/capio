add_subdirectory(collective)
add_subdirectory(config_reader)

add_executable(write_shared_memory write_shared_memory.cpp ../capio_unordered.hpp)
add_executable(write_shared_memory_map write_shared_memory_map.cpp ../capio_unordered.hpp)
add_executable(read_shared_memory read_shared_memory.cpp ../capio_unordered.hpp)
add_executable(read_shared_memory_map read_shared_memory_map.cpp ../capio_unordered.hpp)
add_executable(recv_simple_test recv_simple_test.cpp ../capio_ordered/capio_ordered.hpp)
add_executable(send_simple_test send_simple_test.cpp ../capio_ordered/capio_ordered.hpp)
add_executable(recv_first_last_test recv_first_last_test.cpp ../capio_ordered/capio_ordered.hpp)
add_executable(send_first_last_test send_first_last_test.cpp ../capio_ordered/capio_ordered.hpp)

target_link_libraries(write_shared_memory ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(write_shared_memory_map ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(read_shared_memory ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(read_shared_memory_map ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(recv_simple_test ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(send_simple_test ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(recv_first_last_test ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(send_first_last_test ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(write_shared_memory PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_shared_memory_map PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_shared_memory PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_shared_memory_map PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(recv_simple_test PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(send_simple_test PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(recv_first_last_test PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(send_first_last_test PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(write_shared_memory_map PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_shared_memory PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_shared_memory PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_shared_memory_map PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(recv_simple_test PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(send_simple_test PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(recv_first_last_test PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(send_first_last_test PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

