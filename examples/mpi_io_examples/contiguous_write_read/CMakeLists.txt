add_executable(consecutive_writes consecutive_writes.cpp)
add_executable(print_bin_file_mpi print_bin_file_mpi.cpp)

# Link against MPI
target_link_libraries(consecutive_writes ${MPI_LIBRARIES})
target_link_libraries(print_bin_file_mpi ${MPI_LIBRARIES})

# Include MPI includes
include_directories(${MPI_INCLUDE_PATH})
if (MPI_COMPILE_FLAGS)
    set_target_properties(consecutive_writes PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(print_bin_file_mpi PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif ()

if (MPI_LINK_FLAGS)
    set_target_properties(consecutive_writes PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(print_bin_file_mpi PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif ()