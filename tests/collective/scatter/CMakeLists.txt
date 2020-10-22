add_executable(scatter_write scatter_write.cpp ../../../capio_ordered/capio_ordered.hpp)
add_executable(scatter_read scatter_read.cpp ../../../capio_ordered/capio_ordered.hpp)

target_link_libraries(scatter_write ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(scatter_read ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(scatter_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(scatter_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(scatter_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(scatter_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
