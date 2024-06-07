#ifndef CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP

#include <mpi.h>

#include "remote/backend.hpp"

class MPIBackend : public Backend {

  protected:
    MPI_Request req{};
    int rank = -1;

    /*
     * This structure holds inside the information to convert from hostname to MPI rank*/
    std::set<std::string> nodes;
    std::unordered_map<std::string, std::string> rank_nodes_equivalence;
    static constexpr long MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    MPIBackend(int argc, char **argv) {
        int node_name_len, provided;
        START_LOG(gettid(), "call()");
        LOG("Created a MPI backend");
        MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
        LOG("Mpi has multithreading support? %s (%d)",
            provided == MPI_THREAD_MULTIPLE ? "yes" : "no", provided);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        LOG("node_rank=%d", &rank);
        if (provided != MPI_THREAD_MULTIPLE) {
            LOG("Error: The threading support level is not MPI_THREAD_MULTIPLE (is %d)", provided);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
        MPI_Comm_size(MPI_COMM_WORLD, &n_servers);
        node_name = new char[MPI_MAX_PROCESSOR_NAME];
        MPI_Get_processor_name(node_name, &node_name_len);
        LOG("Node name = %s, length=%d", node_name, node_name_len);
        nodes.emplace(node_name);
        rank_nodes_equivalence[std::to_string(rank)] = node_name;
        rank_nodes_equivalence[node_name]            = std::to_string(rank);
    }

    ~MPIBackend() override {
        START_LOG(gettid(), "Call()");
        MPI_Finalize();
    }

    inline bool store_file_in_memory() override { return true; }

    inline const std::set<std::string> get_nodes() override { return nodes; }

    inline void handshake_servers() override {
        START_LOG(gettid(), "call()");

        auto buf = std::unique_ptr<char[]>(new char[MPI_MAX_PROCESSOR_NAME]);
        for (int i = 0; i < n_servers; i += 1) {
            if (i != rank) {
                // TODO: possible deadlock
                MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD);
                std::fill(buf.get(), buf.get() + MPI_MAX_PROCESSOR_NAME, 0);
                MPI_Recv(buf.get(), MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD,
                         MPI_STATUS_IGNORE);
                nodes.emplace(buf.get());
                rank_nodes_equivalence.emplace(buf.get(), std::to_string(i));
                rank_nodes_equivalence.emplace(std::to_string(i), buf.get());
            }
        }
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
        return {buff, rank_nodes_equivalence[std::to_string(status.MPI_SOURCE)]};
    }

    void send_file(char *shm, long int nbytes, const std::string &target,
                   const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(%.50s, %ld, %s)", shm, nbytes, target.c_str());
        int elem_to_snd = 0;
        int dest        = std::stoi(rank_nodes_equivalence[target]);
        for (long int k = 0; k < nbytes; k += elem_to_snd) {
            // Compute the maximum amount to send for this chunk
            elem_to_snd = static_cast<int>(std::min(nbytes - k, MPI_MAX_ELEM_COUNT));

            LOG("Sending %d bytes to %d with offset from beginning odf k=%ld", elem_to_snd, dest,
                k);
            MPI_Isend(shm + k, elem_to_snd, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req);
            LOG("Sent chunk of %d bytes", elem_to_snd);
        }
    }

    void send_request(const char *message, int message_len, const std::string &target) override {
        START_LOG(gettid(), "call(message=%s, message_len=%d, target=%s)", message, message_len,
                  target.c_str());
        const std::string &mpi_target = rank_nodes_equivalence[target];
        LOG("MPI_rank for target %s is %s", target.c_str(), mpi_target.c_str());

        MPI_Send(message, message_len + 1, MPI_CHAR, std::stoi(mpi_target), 0, MPI_COMM_WORLD);
    }

    inline void recv_file(char *shm, const std::string &source, long int bytes_expected,
                          const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(shm=%ld, source=%s, bytes_expected=%ld)", shm, source.c_str(),
                  bytes_expected);
        MPI_Status status;
        int bytes_received = 0, count = 0, source_rank = std::stoi(rank_nodes_equivalence[source]);
        LOG("Buffer is valid? %s",
            shm != nullptr ? "yes"
                           : "NO! a nullptr was given to recv_file. this will make mpi crash!");
        for (long int k = 0; k < bytes_expected; k += bytes_received) {

            count = static_cast<int>(std::min(bytes_expected - k, MPI_MAX_ELEM_COUNT));

            LOG("Expected %ld bytes from %s with offset from beginning odf k=%ld", count,
                source.c_str(), k);
            MPI_Recv(shm + k, count, MPI_BYTE, source_rank, 0, MPI_COMM_WORLD, &status);
            LOG("Received chunk");
            MPI_Get_count(&status, MPI_BYTE, &bytes_received);
            LOG("Chunk size is %ld bytes", bytes_received);
        }
    }
};

class MPISYNCBackend : public MPIBackend {
  public:
    MPISYNCBackend(int argc, char *argv[]) : MPIBackend(argc, argv) {
        START_LOG(gettid(), "call()");
        LOG("Wrapped MPI backend with MPISYC backend");
    }

    ~MPISYNCBackend() override {
        START_LOG(gettid(), "Call()");
        MPI_Finalize();
    }

    RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        MPI_Status status;
        char *buff = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
        LOG("initiating a synchronized MPI receive");
        MPI_Recv(buff, CAPIO_SERVER_REQUEST_MAX_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
                 &status); // receive from server

        LOG("receive completed!");
        return {buff, rank_nodes_equivalence[std::to_string(status.MPI_SOURCE)]};
    }
};

#endif // CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP