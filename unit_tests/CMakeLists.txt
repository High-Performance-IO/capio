add_executable(simple_write simple_write.cpp)
add_executable(simple_read simple_read.cpp)

target_link_libraries(simple_write ${MPI_LIBRARIES} rt capio_posix)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(simple_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(simple_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
