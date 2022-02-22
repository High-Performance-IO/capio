add_executable(simple_write simple_write.cpp)
add_executable(simple_read simple_read.cpp)
add_executable(simple_write_nc simple_write.cpp)
add_executable(simple_read_nc simple_read.cpp)

target_link_libraries(simple_write ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_read ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_write_nc ${MPI_LIBRARIES} rt)
target_link_libraries(simple_read_nc ${MPI_LIBRARIES} rt)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(simple_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_write_nc PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read_nc PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(simple_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_write_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

add_subdirectory(circular_buffer)
