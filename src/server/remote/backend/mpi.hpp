#ifndef CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP

#include <mpi.h>

#include "remote/backend.hpp"

class MPIBackend : public Backend {

  private:
    MPI_Request req{};
    static constexpr long MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    MPIBackend(int argc, char **argv, int *rank, int *provided) {
        int node_name_len;
        START_LOG(gettid(), "call()");
        MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, provided);
        LOG("Mpi has multithreading support? %s (%d)",
            *provided == MPI_THREAD_MULTIPLE ? "yes" : "no", *provided);
        MPI_Comm_rank(MPI_COMM_WORLD, rank);
        LOG("node_rank=%d", *rank);
        if (*provided != MPI_THREAD_MULTIPLE) {
            LOG("Error: The threading support level is not MPI_THREAD_MULTIPLE (is %d)", *provided);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        node_name = new char[MPI_MAX_PROCESSOR_NAME];
        MPI_Get_processor_name(node_name, &node_name_len);
        LOG("Node name = %s, length=%d", node_name, node_name_len);
    }

    ~MPIBackend() {
        START_LOG(gettid(), "Call()");
        MPI_Finalize();
    }

    inline void handshake_servers(int rank) override {
        START_LOG(gettid(), "call(%d)", rank);

        auto buf = new char[MPI_MAX_PROCESSOR_NAME];
        for (int i = 0; i < n_servers; i += 1) {
            if (i != rank) {
                // TODO: possible deadlock
                MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD);
                std::fill(buf, buf + MPI_MAX_PROCESSOR_NAME, 0);
                MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD,
                         MPI_STATUS_IGNORE);
                nodes_helper_rank[buf] = i;
                rank_to_node[i]        = buf;
            }
        }
        delete[] buf;
    }

    RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        MPI_Status status;
        char *buff = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
        LOG("initiating a lightweight MPI receive");
        MPI_Request request;
        int received = 0;

        // receive from server
        MPI_Irecv(buff, CAPIO_SERVER_REQUEST_MAX_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
                  &request);
        struct timespec sleepTime {};
        struct timespec returnTime {};
        sleepTime.tv_sec  = 0;
        sleepTime.tv_nsec = 200000;

        while (!received) {
            MPI_Test(&request, &received, &status);
            nanosleep(&sleepTime, &returnTime);
        }
        int bytes_received;
        MPI_Get_count(&status, MPI_CHAR, &bytes_received);

        LOG("receive completed!");
        return {buff, status.MPI_SOURCE};
    }

    void send_file(char *shm, long int nbytes, int dest) override {
        START_LOG(gettid(), "call(%.50s, %ld, %d)", shm, nbytes, dest);
        int elem_to_snd = 0;
        for (long int k = 0; k < nbytes; k += elem_to_snd) {
            // Compute the maximum amount to send for this chunk
            elem_to_snd = static_cast<int>(std::min(nbytes - k, MPI_MAX_ELEM_COUNT));

            LOG("Sending %d bytes to %d with offset from beginning odf k=%ld", elem_to_snd, dest,
                k);
            MPI_Isend(shm + k, elem_to_snd, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req);
            LOG("Sent chunk of %d bytes", elem_to_snd);
        }
    }

    void send_request(const char *message, int message_len, int destination) override {
        START_LOG(gettid(), "call(message=%s, message_len=%d, destination=%d)", message,
                  message_len, destination);
        MPI_Send(message, message_len + 1, MPI_CHAR, destination, 0, MPI_COMM_WORLD);
    }

    inline void recv_file(char *shm, int source, long int bytes_expected) override {
        START_LOG(gettid(), "call(shm=%ld, source=%d, length=%ld)", shm, source, bytes_expected);
        MPI_Status status;
        int bytes_received = 0, count = 0;
        LOG("Buffer is valid? %s",
            shm != nullptr ? "yes"
                           : "NO! a nullptr was given to recv_file. this will make mpi crash!");
        for (long int k = 0; k < bytes_expected; k += bytes_received) {

            count = static_cast<int>(std::min(bytes_expected - k, MPI_MAX_ELEM_COUNT));

            LOG("Expected %ld bytes from %d with offset from beginning odf k=%ld", count, source,
                k);
            MPI_Recv(shm + k, count, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);
            LOG("Received chunk");
            MPI_Get_count(&status, MPI_BYTE, &bytes_received);
            LOG("Chunk size is %ld bytes", bytes_received);
        }
    }
};

#endif // CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP