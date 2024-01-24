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

        SEM_DESTROY_CHECK(&this->remote_read_sem, "sem_destroy", MPI_Finalize());
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
        START_LOG(gettid(), "call(%s, %ld, %d)", shm, nbytes, dest);

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

    inline void send_files_batch(const std::string &prefix, std::vector<std::string> *files_to_send,
                                 int nfiles, int dest) override {
        START_LOG(gettid(), "call(prefix=%s, files_to_send=%ld, nfiles=%d, dest=%d)",
                  prefix.c_str(), files_to_send, nfiles, dest);

        std::string msg      = std::to_string(CAPIO_SERVER_REQUEST_SEND_BATCH) + " " + prefix;
        size_t prefix_length = prefix.length();
        for (const std::string &path : *files_to_send) {
            CapioFile &c_file = get_capio_file(path);
            msg.append(" " + path.substr(prefix_length) + " " +
                       std::to_string(c_file.get_stored_size()));
        }

        MPI_Send(msg.c_str(), msg.length() + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);

        for (const std::string &path : *files_to_send) {
            CapioFile &c_file = get_capio_file(path.c_str());
            send_file(c_file.get_buffer(), c_file.get_stored_size(), dest);
        }
    }

    inline void serve_remote_read(const std::filesystem::path &path, int dest, long int offset,
                                  long int nbytes, int complete) override {
        START_LOG(gettid(), "call(pach_c=%s, dest=%d, offset=%ld, nbytes=%ld, complete=%d)",
                  path.c_str(), dest, offset, nbytes, complete);

        SEM_WAIT_CHECK(&remote_read_sem, "remote_read_sems");

        // Send all the rest of the file not only the number of bytes requested
        // Useful for caching
        CapioFile &c_file          = get_capio_file(path);
        nbytes                     = c_file.get_stored_size() - offset;
        off64_t prefetch_data_size = get_prefetch_data_size();

        if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
            nbytes = prefetch_data_size;
        }
        const off64_t file_size = c_file.get_stored_size();

        const char *const format = "%04d %s %ld %ld %d %ld";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_SEND, path.c_str(),
                                  offset, nbytes, complete, file_size);
        const std::unique_ptr<char[]> message(new char[size + 1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_SEND, path.c_str(), offset, nbytes,
                complete, file_size);

        // send warning
        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        // send data
        send_file(c_file.get_buffer() + offset, nbytes, dest);

        SEM_POST_CHECK(&remote_read_sem, "remote_read_sem");
    }

    inline void handle_remote_read(const std::filesystem::path &path, off64_t offset, off64_t count,
                                   int rank) override {
        START_LOG(gettid(), "call(path=%d, offset=%d, count=%ld, rank=%d)", path.c_str(), offset,
                  count, rank);

        // If it is not in cache then send the request to the remote node
        int dest = nodes_helper_rank[std::get<0>(get_file_location(path))];

        const char *const format = "%04d %s %d %ld %ld";
        const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ, path.c_str(), rank,
                                  offset, count);
        const std::unique_ptr<char[]> message(new char[size + 1]);
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ, path.c_str(), rank, offset,
                count);
        LOG("Message = %s", message.get());
        MPI_Send(message.get(), size + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    }

    inline bool handle_nreads(const std::filesystem::path &path, const std::string &app_name,
                              int dest) override {
        START_LOG(gettid(), "call(path=%s, app_name=%s, dest=%d)", path.c_str(), app_name.c_str(),
                  dest);

        long int pos = match_globs(path);
        if (pos != -1) {
            std::string glob       = std::get<0>(metadata_conf_globs[pos]);
            std::size_t batch_size = std::get<5>(metadata_conf_globs[pos]);
            if (batch_size > 0) {
                auto msg = new char[sizeof(char) * (512 + PATH_MAX)];
                sprintf(msg, "%04d %zu %s %s %s", CAPIO_SERVER_REQUEST_READ_BATCH, batch_size,
                        app_name.c_str(), glob.c_str(), path.c_str());
                MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
                delete[] msg;
                return true;
            }
        }
        return false;
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
        sprintf(message.get(), format, CAPIO_SERVER_REQUEST_STAT_REPLY, path.c_str(), source_tid,
                file_size, is_dir);
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