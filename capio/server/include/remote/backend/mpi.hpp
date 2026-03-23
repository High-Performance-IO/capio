#ifndef CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#include <mpi.h>
#include <unordered_map>

#include "remote/backend.hpp"

class MPIBackend : public Backend {

  protected:
    MPI_Request req{};
    int rank = -1;

    /// This structure holds inside the information to convert from hostname to MPI rank
    std::set<std::string> nodes;
    std::unordered_map<std::string, std::string> rank_nodes_equivalence;
    static constexpr long MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    MPIBackend(int argc, char **argv);

    ~MPIBackend() override;
    std::set<std::string> get_nodes() override;
    void handshake_servers() override;
    RemoteRequest read_next_request() override;
    void send_file(char *shm, long int nbytes, const std::string &target) override;
    void send_request(const char *message, int message_len, const std::string &target) override;
    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;
};

class MPISYNCBackend : public MPIBackend {
  public:
    MPISYNCBackend(int argc, char *argv[]);
    ~MPISYNCBackend() override;
    RemoteRequest read_next_request() override;
};

#endif // CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP