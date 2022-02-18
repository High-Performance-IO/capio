#include <iostream>
#include <string.h>
#include <mpi.h>

void err_exit(std::string error_msg) {
	std::cerr << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
	MPI_Abort(MPI_COMM_WORLD, 1);
}
