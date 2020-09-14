
add_executable(reduce_write reduce_write.cpp ../../../capio_mpi/capio_mpi.hpp)
add_executable(reduce_read reduce_read.cpp ../../../capio_mpi/capio_mpi.hpp)

target_link_libraries(reduce_write ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(reduce_read ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(reduce_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(reduce_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(reduce_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(reduce_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
