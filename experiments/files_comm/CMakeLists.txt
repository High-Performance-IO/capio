add_executable(write_matrix_independent write_matrix_independent.cpp)
add_executable(reduce_matrix_independent_one_elem reduce_matrix_independent_one_elem.cpp)
add_executable(reduce_matrix_independent_one_matrix reduce_matrix_independent_one_matrix.cpp)
add_executable(gather_read_exp gather_read_exp.cpp)
add_executable(write_one_matrix write_one_matrix.cpp)
add_executable(read_one_matrix read_one_matrix.cpp)
add_executable(read_all_gather read_all_gather.cpp)
add_executable(write_scatter write_scatter.cpp)
add_executable(read_scatter read_scatter.cpp)
add_executable(read_matrixes read_matrixes.cpp)
add_executable(read_all_reduce read_all_reduce.cpp)
add_executable(read_all_to_all read_all_to_all.cpp)
add_executable(write_broadcast write_broadcast.cpp)
add_executable(read_broadcast read_broadcast.cpp)


target_link_libraries(write_matrix_independent ${MPI_LIBRARIES} rt)
target_link_libraries(reduce_matrix_independent_one_elem ${MPI_LIBRARIES} rt)
target_link_libraries(reduce_matrix_independent_one_matrix ${MPI_LIBRARIES} rt)
target_link_libraries(gather_read_exp ${MPI_LIBRARIES} rt)
target_link_libraries(write_one_matrix ${MPI_LIBRARIES} rt)
target_link_libraries(read_one_matrix ${MPI_LIBRARIES} rt)
target_link_libraries(read_all_gather ${MPI_LIBRARIES} rt)
target_link_libraries(write_scatter ${MPI_LIBRARIES} rt)
target_link_libraries(read_scatter ${MPI_LIBRARIES} rt)
target_link_libraries(read_matrixes ${MPI_LIBRARIES} rt)
target_link_libraries(read_all_reduce ${MPI_LIBRARIES} rt)
target_link_libraries(read_all_to_all ${MPI_LIBRARIES} rt)
target_link_libraries(write_broadcast ${MPI_LIBRARIES} rt)
target_link_libraries(read_broadcast ${MPI_LIBRARIES} rt)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(write_matrix_independent PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(reduce_matrix_independent_one_elem PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(reduce_matrix_independent_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(gather_read_exp PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_one_matrix PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_all_gather PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_scatter PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_scatter PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_matrixes PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_all_reduce PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_all_to_all PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(write_broadcast PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(read_broadcast PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(write_matrix_independent PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(reduce_matrix_independent_one_elem PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(reduce_matrix_independent_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(gather_read_exp PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_one_matrix PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_all_gather PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_scatter PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_scatter PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_matrixes PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_all_reduce PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_all_to_all PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(write_broadcast PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(read_broadcast PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()