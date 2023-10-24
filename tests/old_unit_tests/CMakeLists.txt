add_executable(simple_write simple_write.cpp)
add_executable(simple_write_fopen simple_write_fopen.cpp)
add_executable(simple_read simple_read.cpp)
add_executable(simple_read_fopen simple_read_fopen.cpp)
add_executable(read_write_mpi read_write_mpi.cpp)
add_executable(request_reply_mpi request_reply_mpi.cpp)

add_executable(simple_fopen simple_fopen.cpp)

add_executable(capio_file_tests capio_file_tests.cpp)


add_executable(writev_test writev_test.cpp)

target_link_libraries(simple_write ${MPI_LIBRARIES} rt)
target_link_libraries(simple_write_fopen ${MPI_LIBRARIES} rt)
target_link_libraries(simple_read ${MPI_LIBRARIES} rt)
target_link_libraries(simple_read_fopen ${MPI_LIBRARIES} rt)
target_link_libraries(read_write_mpi ${MPI_LIBRARIES} rt)
target_link_libraries(request_reply_mpi ${MPI_LIBRARIES} rt)
target_link_libraries(writev_test)


target_link_libraries(simple_fopen  rt)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(simple_write PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_write_fopen PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read_fopen PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_write_mpi PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(request_reply_mpi PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(simple_write PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_write_fopen PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read_fopen PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_write_mpi PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(request_reply_mpi PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

add_subdirectory(circular_buffer)
