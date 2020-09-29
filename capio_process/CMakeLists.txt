add_executable(capio capio.cpp)
add_executable(capio_clean capio_clean.cpp)

target_link_libraries(capio ${MPI_LIBRARIES} rt Threads::Threads)
target_link_libraries(capio_clean ${MPI_LIBRARIES} rt Threads::Threads)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
    set_target_properties(capio PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(capio_clean PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
    set_target_properties(capio PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(capio_clean PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

