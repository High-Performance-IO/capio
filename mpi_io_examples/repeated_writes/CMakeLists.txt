add_executable(repeated_writes repeated_writes.cpp)

# Link against MPI
target_link_libraries(repeated_writes ${MPI_LIBRARIES})

# Include MPI includes
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(repeated_writes PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(repeated_writes PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()