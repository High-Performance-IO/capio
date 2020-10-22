add_executable(capio_write_reduce_matrix_one_matrix capio_write_reduce_matrix_one_matrix.cpp)
add_executable(capio_read_reduce_matrix_one_matrix capio_read_reduce_matrix_one_matrix.cpp)
add_executable(capio_send_matrix_to_one capio_send_matrix_to_one.cpp)
add_executable(capio_read_gather capio_read_gather.cpp)
add_executable(capio_write_gather capio_write_gather.cpp)
add_executable(capio_read_one_matrix capio_read_one_matrix.cpp)
add_executable(capio_write_one_matrix capio_write_one_matrix.cpp)
add_executable(capio_write_all_gather capio_write_all_gather.cpp)
add_executable(capio_read_all_gather capio_read_all_gather.cpp)
add_executable(read_scatter_exp read_scatter_exp.cpp)
add_executable(write_scatter_exp write_scatter_exp.cpp)
add_executable(capio_read_matrixes capio_read_matrixes.cpp)
add_executable(capio_write_matrixes capio_write_matrixes.cpp)
add_executable(capio_read_all_reduce capio_read_all_reduce.cpp)
add_executable(capio_write_all_reduce capio_write_all_reduce.cpp)
add_executable(capio_read_all_to_all capio_read_all_to_all.cpp)
add_executable(capio_write_all_to_all capio_write_all_to_all.cpp)
add_executable(capio_read_broadcast capio_read_broadcast.cpp)
add_executable(capio_write_broadcast capio_write_broadcast.cpp)

target_link_libraries(capio_write_reduce_matrix_one_matrix ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_reduce_matrix_one_matrix ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_send_matrix_to_one ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_gather ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_gather ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_one_matrix ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_one_matrix ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_all_gather ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_all_gather ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(read_scatter_exp ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(write_scatter_exp ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_matrixes ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_matrixes ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_all_reduce ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_all_reduce ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_all_to_all ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_all_to_all ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_read_broadcast ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_write_broadcast ${MPI_LIBRARIES} rt Threads::Threads)

# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(capio_write_reduce_matrix_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_reduce_matrix_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_send_matrix_to_one PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_gather PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_gather PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_all_gather PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_all_gather PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_scatter_exp PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_scatter_exp PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_matrixes PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_matrixes PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_all_reduce PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_all_reduce PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_all_to_all PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_all_to_all PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_read_broadcast PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_write_broadcast PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(capio_write_reduce_matrix_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_reduce_matrix_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_send_matrix_to_one PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_gather PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_gather PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_all_gather PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_all_gather PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_scatter_exp PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_scatter_exp PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_matrixes PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_matrixes PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_all_reduce PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_all_reduce PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_all_to_all PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_all_to_all PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_read_broadcast PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_write_broadcast PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
