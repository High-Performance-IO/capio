#ifdef CAPIO_HAS_MPI
#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "child.hpp"
#include "common/requests.hpp"
#include "remote/backend/mpi.hpp"

/*
 * One server alone, started without mpirun (an MPI singleton): it sends to itself. Each test runs
 * in a child, as MPI can be initialized only once in a process.
 */
template <typename MPIBackendType> void sendsToItself() {
    std::array<char *, 2> argv{const_cast<char *>("capio_server"), nullptr}; // as main's argv
    MPIBackendType mpi_backend(1, argv.data());
    mpi_backend.handshake_servers();
    const auto &name = mpi_backend.get_node_name();
    EXPECT_EQ(mpi_backend.get_nodes(), std::set<std::string>{name});
    mpi_backend.connect_to("unused"); // MPI knows all the servers from the start

    const std::string request = "0002 a request";
    mpi_backend.send_request(request.c_str(), static_cast<int>(request.size()), name);
    const RemoteRequest received = mpi_backend.read_next_request();
    EXPECT_EQ(received.get_code(), CAPIO_SERVER_REQUEST_STAT);
    EXPECT_STREQ(received.get_content(), "a request");
    EXPECT_EQ(received.get_source(), name);

    const std::string header = "0001 a file";
    char data[]              = "0123456789";
    mpi_backend.send_file(header.c_str(), static_cast<int>(header.size()), data, 10, name);
    const RemoteRequest with_file = mpi_backend.read_next_request();
    EXPECT_EQ(with_file.get_code(), CAPIO_SERVER_REQUEST_READ_REPLY);
    std::array<char, 10> file{};
    mpi_backend.recv_file(file.data(), file.size(), with_file.get_source());
    EXPECT_EQ(std::string(file.data(), file.size()), "0123456789");
}

TEST(MPIBackendTest, AServerAloneSendsRequestsAndFilesToItself) {
    EXPECT_EQ(run_in_child(sendsToItself<MPIBackend>), 0);
}

TEST(MPIBackendTest, TheSynchronousVariantSendsRequestsAndFilesToItself) {
    EXPECT_EQ(run_in_child(sendsToItself<MPISYNCBackend>), 0);
}
#endif
