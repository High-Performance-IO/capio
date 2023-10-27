add_executable(producer producer.cpp)
add_executable(consumer consumer.cpp)
add_executable(producer_perf producer_perf.cpp)
add_executable(consumer_perf consumer_perf.cpp)

target_link_libraries(producer ${MPI_LIBRARIES} rt)
target_link_libraries(consumer ${MPI_LIBRARIES} rt)
target_link_libraries(producer_perf ${MPI_LIBRARIES} rt)
target_link_libraries(consumer_perf ${MPI_LIBRARIES} rt)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if (MPI_COMPILE_FLAGS)
    set_target_properties(producer PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(consumer PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(producer_perf PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(consumer_perf PROPERTIES
            COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif ()

if (MPI_LINK_FLAGS)
    set_target_properties(producer PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(consumer PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(producer_perf PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(consumer_perf PROPERTIES
            LINK_FLAGS "${MPI_LINK_FLAGS}")
endif ()
