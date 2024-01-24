#ifndef CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP

#include <mpi.h>

#include "remote/backend.hpp"

class MPIBackend : public Backend {

  private:
    MPI_Request req{};
    static constexpr long MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    void initialize(int argc, char **argv, int *rank, int *provided) override {
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

    inline void destroy() override {
        START_LOG(gettid(), "Call()");
        MPI_Finalize();
    }

    inline void handshake_servers(int rank) override {
        START_LOG(gettid(), "call(%d)", rank);

        auto buf = new char[MPI_MAX_PROCESSOR_NAME];

        if (rank == 0) {
            clean_files_location();
        }
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
#ifdef CAPIOSYNC
        LOG("initiating a synchronized MPI receive");
        MPI_Recv(buff, CAPIO_SERVER_REQUEST_MAX_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
                 &status); // receive from server
#else
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
#endif

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

    inline void send_files_batch(const std::string &prefix, int dest, int tid, int fd,
                                 off64_t count, bool is_getdents,
                                 const std::vector<std::string> *files_to_send) override {
        START_LOG(gettid(), "call(prefix=%s, dest=%d, tid=%d, fd=%d, count=%ld, is_getdents=%s)",
                  prefix.c_str(), dest, tid, fd, count, is_getdents ? "true" : "false");

        const char *const format = "%04d %s %d %d %ld %d";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_BATCH_REPLY,
                                  prefix.c_str(), tid, fd, count, is_getdents);
        const std::unique_ptr<char[]> header(new char[size+1]);
        sprintf(header.get(), format, CAPIO_SERVER_REQUEST_READ_BATCH_REPLY, prefix.c_str(), tid,
                fd, count, is_getdents);
        std::string message(header.get());
        for (const std::string &path : *files_to_send) {
            CapioFile &c_file = get_capio_file(path);
            message.append(" " + path.substr(prefix.length()) + " " +
                           std::to_string(c_file.get_stored_size()));
        }
        LOG("Message = %s", message.c_str());

        // send request
        MPI_Send(message.c_str(), static_cast<int>(message.length()) + 1, MPI_CHAR, dest, 0,
                 MPI_COMM_WORLD);
        // send data
        for (const std::string &path : *files_to_send) {
            CapioFile &c_file = get_capio_file(path);
            send_file(c_file.get_buffer(), c_file.get_stored_size(), dest);
        }
    }

    inline void serve_remote_read(const std::filesystem::path &path, int dest, int tid, int fd,
                                  off64_t count, off64_t offset, bool complete,
                                  bool is_getdents) override {
        START_LOG(gettid(),
                  "call(path=%s, dest=%d, tid=%d, fd=%d, count=%ld, offset=%ld, complete=%s, "
                  "is_getdents=%s)",
                  path.c_str(), dest, tid, fd, offset, complete ? "true" : "false",
                  is_getdents ? "true" : "false");

        // Send all the rest of the file not only the number of bytes requested
        // Useful for caching
        CapioFile &c_file          = get_capio_file(path);
        long int nbytes            = c_file.get_stored_size() - offset;
        off64_t prefetch_data_size = get_prefetch_data_size();

        if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
            nbytes = prefetch_data_size;
        }
        const off64_t file_size = c_file.get_stored_size();

        const char *const format = "%04d %d %d %ld %ld %ld %d %d";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd,
                                  count, nbytes, file_size, complete, is_getdents);
        const std::unique_ptr<char[]> message(new char[size+1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd, count, nbytes,
                file_size, complete, is_getdents);
        LOG("Message = %s", message.get());

        // send request
        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        // send data
        send_file(c_file.get_buffer() + offset, nbytes, dest);
    }

    inline void handle_remote_read(int tid, int fd, off64_t count, bool is_getdents) override {
        START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_getdents=%s)", tid, fd, count,
                  is_getdents ? "true" : "false");

        // If it is not in cache then send the request to the remote node
        const std::filesystem::path &path = get_capio_file_path(tid, fd);
        off64_t offset                    = get_capio_file_offset(tid, fd);
        int dest                          = nodes_helper_rank[std::get<0>(get_file_location(path))];

        const char *const format = "%04d %s %d %d %ld %ld %d";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid,
                                  fd, count, offset, is_getdents);
        const std::unique_ptr<char[]> message(new char[size + 1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid, fd, count,
                offset, is_getdents);

        LOG("Message = %s", message.get());
        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    }

    inline void handle_remote_read_batch(int tid, int fd, off64_t count,
                                         const std::string &app_name, const std::string &prefix,
                                         off64_t batch_size, bool is_getdents) override {
        START_LOG(gettid(),
                  "call(tid=%d, fd=%d, count=%ld, app_name=%s, prefix=%s, "
                  "batch_size=%ld, is_getdents=%s)",
                  tid, fd, count, app_name.c_str(), prefix.c_str(), batch_size,
                  is_getdents ? "true" : "false");

        const std::filesystem::path &path = get_capio_file_path(tid, fd);
        int dest                          = nodes_helper_rank[std::get<0>(get_file_location(path))];

        const char *const format = "%04d %s %d %d %ld %ld %s %s %d";
        const int size =
            snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_BATCH, path.c_str(), tid, fd,
                     count, batch_size, app_name.c_str(), prefix.c_str(), is_getdents);
        const std::unique_ptr<char[]> message(new char[size + 1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ_BATCH, path.c_str(), tid, fd,
                count, batch_size, app_name.c_str(), prefix.c_str(), is_getdents);
        LOG("Message = %s", message.get());
        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    }

    inline void serve_remote_stat(const std::filesystem::path &path, int dest,
                                  int source_tid) override {
        START_LOG(gettid(), "call(path=%s, dest=%d, source_tid%d)", path.c_str(), dest, source_tid);

        const CapioFile &c_file  = get_capio_file(path);
        off64_t file_size        = c_file.get_file_size();
        bool is_dir              = c_file.is_dir();
        const char *const format = "%04d %s %d %ld %d";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_STAT_REPLY, path.c_str(),
                                  source_tid, file_size, is_dir);
        const std::unique_ptr<char[]> message(new char[size + 1]);

        sprintf(message.get(), "%04d %s %d %ld %d", CAPIO_SERVER_REQUEST_STAT_REPLY, path.c_str(),
                source_tid, file_size, is_dir);

        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    }

    inline void handle_remote_stat(int tid, const std::filesystem::path &path, int rank) override {
        START_LOG(gettid(), "call(tid=%d, path=%s, rank=%d)", tid, path.c_str(), rank);

        int dest                 = nodes_helper_rank[std::get<0>(get_file_location(path))];
        const char *const format = "%04d %d %d %s";
        const int size =
            snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_STAT, tid, rank, path.c_str());
        const std::unique_ptr<char[]> message(new char[size + 1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_STAT, tid, rank, path.c_str());
        LOG("destination=%d, message=%s", dest, message.get());

        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        LOG("message sent");
    }

    inline void recv_file(char *shm, int source, long int bytes_expected) override {
        START_LOG(gettid(), "call(shm=%ld, source=%d, length=%ld)", shm, source, bytes_expected);
        MPI_Status status;
        int bytes_received = 0, count = 0;
        LOG("Buffer is valid? %s",
            shm != nullptr ? "yes"
                           : "NO! a nullptr was given to receive. this will make mpi crash!");
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

}; // namespace backend

#endif // CAPIO_SERVER_REMOTE_BACKEND_MPI_HPP