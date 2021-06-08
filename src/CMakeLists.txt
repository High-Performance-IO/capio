set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")

add_executable(pipe_fs pipe_fs.cpp)

target_link_libraries(pipe_fs ${MPI_LIBRARIES} rt Threads::Threads fuse3)
# includes MPI
include_directories(${MPI_INCLUDE_PATH})
if(MPI_COMPILE_FLAGS)
	set_target_properties(pipe_fs PROPERTIES
		COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

if(MPI_LINK_FLAGS)
	set_target_properties(pipe_fs PROPERTIES
		LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()
