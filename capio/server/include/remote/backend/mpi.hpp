#ifndef CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#include <mpi.h>
#include <mutex>
#include <unordered_map>

#include "remote/backend.hpp"

class MPIBackend : public Backend {

  protected:
    MPI_Request req{};
    int rank = -1;

    /// Serializes the sends of all threads. MPI orders the messages of one sender to one
    /// destination only when the program orders them: without this, a request sent by one thread
    /// could arrive between the header and the data of a file sent by another, where recv_file
    /// would take it for data.
    std::mutex send_mutex;

    /// This structure holds inside the information to convert from hostname to MPI rank
    std::set<std::string> nodes;
    std::unordered_map<std::string, int> hostname_to_rank;
    std::unordered_map<int, std::string> rank_to_hostname;
    static constexpr long MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    MPIBackend(int argc, char **argv);

    ~MPIBackend() override;
    const std::set<std::string> get_nodes() override;
    void handshake_servers() override;
    RemoteRequest read_next_request() override;
    void send_file(const char *message, int message_len, char *shm, long int nbytes,
                   const std::string &target) override;
    void send_request(const char *message, int message_len, const std::string &target) override;
    void recv_file(char *shm, long int bytes_expected, const std::string &source) override;
    void connect_to(const std::string &target) override;
};

class MPISYNCBackend final : public MPIBackend {
  public:
    MPISYNCBackend(int argc, char *argv[]);
    ~MPISYNCBackend() override;
    RemoteRequest read_next_request() override;
};

#endif // CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
