add_executable(all_gather_write all_gather_write.cpp ../../../capio_mpi/capio_mpi.hpp)
add_executable(all_gather_read all_gather_read.cpp ../../../capio_mpi/capio_mpi.hpp)

target_link_libraries(all_gather_write ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(all_gather_read ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(all_gather_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(all_gather_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(all_gather_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(all_gather_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

configure_file(all_gather_write.cpp run_all_gather_tests.sh  COPYONLY)