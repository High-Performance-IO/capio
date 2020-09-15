add_executable(gather_all_write gather_all_write.cpp ../../../capio_mpi/capio_mpi.hpp)
add_executable(gather_all_read gather_all_read.cpp ../../../capio_mpi/capio_mpi.hpp)

target_link_libraries(gather_all_write ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(gather_all_read ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(gather_all_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(gather_all_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(gather_all_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(gather_all_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
