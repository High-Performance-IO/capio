#include "remote/backend/mpi.hpp"

MPISYNCBackend::MPISYNCBackend(int argc, char *argv[]) : MPIBackend(argc, argv) {
    START_LOG(gettid(), "call()");
    LOG("Wrapped MPI backend with MPISYC backend");
}

MPISYNCBackend::~MPISYNCBackend() {
    START_LOG(gettid(), "Call()");
    MPI_Finalize();
}

RemoteRequest MPISYNCBackend::read_next_request() {
    START_LOG(gettid(), "call()");
    MPI_Status status;
    char *buff = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
    LOG("initiating a synchronized MPI receive");
    MPI_Recv(buff, CAPIO_SERVER_REQUEST_MAX_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
             &status); // receive from server

    LOG("receive completed!");
    return {buff, rank_nodes_equivalence[std::to_string(status.MPI_SOURCE)]};
}