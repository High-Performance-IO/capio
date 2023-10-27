add_executable(create_matrix create_matrix.cpp)
add_executable(read_matrix read_matrix.cpp)
# Link against MPI
target_link_libraries(create_matrix ${MPI_LIBRARIES})
target_link_libraries(read_matrix ${MPI_LIBRARIES})
# Include MPI includes
include_directories(${MPI_INCLUDE_PATH})
if (MPI_COMPILE_FLAGS)
    set_target_properties(create_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif ()
if (MPI_LINK_FLAGS)
    set_target_properties(create_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif ()