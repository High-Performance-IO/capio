add_executable(simple_write simple_write.cpp)
add_executable(simple_write_fopen simple_write_fopen.cpp)
add_executable(simple_read simple_read.cpp)
add_executable(simple_read_fopen simple_read_fopen.cpp)
add_executable(simple_write_nc simple_write.cpp)
add_executable(simple_write_fopen_nc simple_write_fopen.cpp)
add_executable(simple_read_nc simple_read.cpp)
add_executable(simple_read_fopen_nc simple_read_fopen.cpp)
add_executable(read_write_mpi read_write_mpi.cpp)
add_executable(request_reply_mpi request_reply_mpi.cpp)

add_executable(simple_fopen simple_fopen.cpp)
add_executable(simple_fopen_nc simple_fopen.cpp)

add_executable(capio_file_tests capio_file_tests.cpp)
add_executable(test_lseek_write test_lseek_write.cpp)
add_executable(test_lseek_read test_lseek_read.cpp)


add_executable(writev_test writev_test.cpp)
add_executable(writev_test_nc writev_test.cpp)

target_link_libraries(simple_write ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_write_fopen ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_read ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_read_fopen ${MPI_LIBRARIES} rt capio_posix)
target_link_libraries(simple_write_nc ${MPI_LIBRARIES} rt)
target_link_libraries(simple_write_fopen_nc ${MPI_LIBRARIES} rt)
target_link_libraries(simple_read_nc ${MPI_LIBRARIES} rt)
target_link_libraries(simple_read_fopen_nc ${MPI_LIBRARIES} rt)
target_link_libraries(read_write_mpi ${MPI_LIBRARIES} rt)
target_link_libraries(request_reply_mpi ${MPI_LIBRARIES} rt)
target_link_libraries(test_lseek_write rt capio_posix)
target_link_libraries(writev_test capio_posix)
target_link_libraries(test_lseek_read rt capio_posix)

target_link_libraries(simple_fopen  rt capio_posix)
target_link_libraries(simple_fopen_nc rt)

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
    set_target_properties(simple_write_nc PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_write_fopen_nc PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read_nc PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(simple_read_fopen_nc PROPERTIES
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
    set_target_properties(simple_write_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_write_fopen_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(simple_read_fopen_nc PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_write_mpi PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(request_reply_mpi PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

add_subdirectory(circular_buffer)
