add_executable(broadcast_write broadcast_write.cpp ../../../capio_mpi/capio_mpi.hpp)
add_executable(broadcast_read broadcast_read.cpp ../../../capio_mpi/capio_mpi.hpp)

target_link_libraries(broadcast_write ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(broadcast_read ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(broadcast_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(broadcast_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(broadcast_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(broadcast_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
