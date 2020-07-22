add_executable(write_shared_memory write_shared_memory.cpp ../capio.hpp)
add_executable(read_shared_memory read_shared_memory.cpp ../capio.hpp)

target_link_libraries(write_shared_memory ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(read_shared_memory ${MPI_LIBRARIES} rt Threads::Threads)
# Include MPI includes
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(write_shared_memory PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_shared_memory PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(write_shared_memory PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_shared_memory PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

