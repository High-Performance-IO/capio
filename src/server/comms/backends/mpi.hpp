#ifndef CAPIO_CAPIO_COMMS_H
#define CAPIO_CAPIO_COMMS_H

#include "../interfaces.hpp"
#include <mpi.h>

// TODO: move this outside this file once circular dependencies between communication layer and
// server functions are resolved
extern backend_interface *backend;

class MPI_backend : public backend_interface {

  private:
    MPI_Request req{};
    long int MPI_MAX_ELEM_COUNT = 1024L * 1024 * 1024;

  public:
    void initialize(int argc, char **argv, int *rank, int *provided) override {
        int node_name_len;
        START_LOG(gettid(), "call()");
        MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, provided);
        MPI_Comm_rank(MPI_COMM_WORLD, rank);
        LOG("node_rank=%d", rank);
        if (*provided != MPI_THREAD_MULTIPLE) {
            LOG("Error: The threading support level is lesser than that demanded");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        node_name = new char[MPI_MAX_PROCESSOR_NAME];
        MPI_Get_processor_name(node_name, &node_name_len);
        LOG("Node name = %s, length=%d", node_name, node_name_len);
    }

    inline void destroy(std::vector<sem_t *> *sems) override {
        START_LOG(gettid(), "Call()");
        for (auto sem_to_destroy : *sems) {
            SEM_DESTROY_CHECK(sem_to_destroy, "sem_destroy", MPI_Finalize());
        }
        LOG("Semaphores deleted. finalizing MPI");
        MPI_Finalize();
    }

    inline void handshake_servers(int rank) override {
        START_LOG(gettid(), "call(%d)", rank);

        auto buf = new char[MPI_MAX_PROCESSOR_NAME];

        if (rank == 0) {
            clean_files_location(n_servers);
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

    RemoteRequest *read_next_request() override {
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
        MPI_Get_count(&status, MPI_BYTE, &bytes_received);
#endif

        LOG("receive completed!");
        return new RemoteRequest(buff, status.MPI_SOURCE);
    }

    void send_file(char *shm, long int nbytes, int dest) override {
        START_LOG(gettid(), "call(%s), %ld, %d", shm, nbytes, dest);

        long int elem_to_snd = 0;

        for (long int k = 0; k < nbytes; k += elem_to_snd) {
            (nbytes - k > MPI_MAX_ELEM_COUNT) ? elem_to_snd = MPI_MAX_ELEM_COUNT
                                              : elem_to_snd = nbytes - k;

            MPI_Isend(shm + k, elem_to_snd, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req);
        }
    }

    inline void send_n_files(const std::string &prefix, std::vector<std::string> *files_to_send,
                             int n_files, int dest) override {
        START_LOG(gettid(), "call(prefix=%s, files_to_send=%ld, n_files=%d, dest=%d)",
                  prefix.c_str(), files_to_send, n_files, dest);

        std::string msg      = std::to_string(CAPIO_SERVER_REQUEST_N_SEND) + " " + prefix;
        size_t prefix_length = prefix.length();
        for (const std::string &path : *files_to_send) {
            Capio_file &c_file = get_capio_file(path.c_str());
            msg.append(" " + path.substr(prefix_length) + " " +
                       std::to_string(c_file.get_stored_size()));
        }

        MPI_Send(msg.c_str(), msg.length() + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);

        for (const std::string &path : *files_to_send) {
            Capio_file c_file = get_capio_file(path.c_str());
            send_file(c_file.get_buffer(), c_file.get_stored_size(), dest);
        }
    }

    inline void serve_remote_read(const char *path_c, int dest, long int offset, long int nbytes,
                                  int complete) override {
        START_LOG(gettid(), "call(pach_c=%s, dest=%d, offset=%ld, nbytes=%ld, complete=%d)", path_c,
                  dest, offset, nbytes, complete);

        SEM_WAIT_CHECK(&remote_read_sem, "remote_read_sems");

        // Send all the rest of the file not only the number of bytes requested
        // Useful for caching
        Capio_file &c_file         = get_capio_file(path_c);
        nbytes                     = c_file.get_stored_size() - offset;
        off64_t prefetch_data_size = get_prefetch_data_size();

        if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
            nbytes = prefetch_data_size;
        }

        std::string nbytes_str    = std::to_string(nbytes);
        std::string offset_str    = std::to_string(offset);
        std::string complete_str  = std::to_string(complete);
        std::string file_size_str = std::to_string(c_file.get_stored_size());

        auto buf_send =
            new char[sizeof(int) + strlen(path_c) + offset_str.length() + nbytes_str.length() +
                     complete_str.length() + file_size_str.length() + 6];

        // TODO:add malloc check
        sprintf(buf_send, "%d %s %s %s %s %s", CAPIO_SERVER_REQUEST_SENDING, path_c,
                offset_str.c_str(), nbytes_str.c_str(), complete_str.c_str(),
                file_size_str.c_str());

        // send warning
        MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        // send data
        send_file(c_file.get_buffer() + offset, nbytes, dest);

        SEM_POST_CHECK(&remote_read_sem, "remote_read_sem");

        delete[] buf_send;
    }

    inline void handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir,
                                   bool is_getdents, CSMyRemotePendingReads_t *pending_remote_reads,
                                   std::mutex *pending_remote_reads_mutex,
                                   void (*handle_local_read)(int, int, off64_t, bool, bool,
                                                             bool)) override {

        START_LOG(tid, "call(tid=%d, fd=%d, count=%ld, rank=%d, dir=%s, is_getdents=%s)", tid, fd,
                  count, rank, dir ? "true" : "false", is_getdents ? "true" : "false");

        // before sending the request to the remote node, it checks
        // in the local cache
        const std::lock_guard<std::mutex> lg(*pending_remote_reads_mutex);
        std::string_view path  = get_capio_file_path(tid, fd);
        Capio_file &c_file     = get_capio_file(path.data());
        off64_t process_offset = get_capio_file_offset(tid, fd);
        off64_t end_of_read    = process_offset + count;
        off64_t end_of_sector  = c_file.get_sector_end(process_offset);

        if (c_file.complete &&
            (end_of_read <= end_of_sector ||
             (end_of_sector == -1 ? 0 : end_of_sector) == c_file.real_file_size)) {
            handle_local_read(tid, fd, count, dir, is_getdents, true);
            return;
        }
        // when is not complete but mode = append
        if (read_from_local_mem(tid, process_offset, end_of_read, end_of_sector, count,
                                path.data())) {
            // it means end_of_read < end_of_sector
            return;
        }

        // If it is not in cache then send the request to the remote node

        int dest      = nodes_helper_rank[std::get<0>(get_file_location(path.data()))];
        size_t offset = get_capio_file_offset(tid, fd);

        auto message = std::to_string(CAPIO_SERVER_REQUEST_READ) + " " + std::string(path) + " " +
                       std::to_string(rank) + " " + std::to_string(offset) + " " +
                       std::to_string(count);

        MPI_Send(message.c_str(), message.length() + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        (*pending_remote_reads)[path.data()].emplace_back(tid, fd, count, is_getdents);
    }

    inline bool handle_nreads(const std::string &path, const std::string &app_name,
                              int dest) override {
        START_LOG(gettid(), "call(path=%s, app_name=%s, dest=%d)", path.c_str(), app_name.c_str(),
                  dest);

        long int pos = match_globs(path);
        if (pos != -1) {
            std::string glob       = std::get<0>(metadata_conf_globs[pos]);
            std::size_t batch_size = std::get<5>(metadata_conf_globs[pos]);
            if (batch_size > 0) {
                auto msg = new char[sizeof(char) * (512 + PATH_MAX)];
                sprintf(msg, "%d %zu %s %s %s", CAPIO_SERVER_REQUEST_N_READ, batch_size,
                        app_name.c_str(), glob.c_str(), path.c_str());
                MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
                delete[] msg;
                return true;
            }
        }
        return false;
    }

    inline void serve_remote_stat(const char *path, int dest, const Capio_file &c_file) override {
        START_LOG(gettid(), "call(%s, %d, %ld)", path, dest, c_file.get_buf_size());
        auto msg = new char[PATH_MAX + 1024];

        sprintf(msg, "%d %s %ld %d", CAPIO_SERVER_REQUEST_SIZE, path, c_file.get_file_size(),
                c_file.is_dir() ? 1 : 0);

        MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    }

    inline void handle_remote_stat(int tid, const std::string &path, int rank,
                                   CSMyRemotePendingStats_t *pending_remote_stats,
                                   std::mutex *pending_remote_stats_mutex) override {
        START_LOG(tid, "call(tid=%d, path=%s, rank=%d)", tid, path.c_str(), rank);

        const std::lock_guard<std::mutex> lg(*pending_remote_stats_mutex);

        auto dest = nodes_helper_rank[std::get<0>(get_file_location(path.c_str()))];
        auto msg =
            std::to_string(CAPIO_SERVER_REQUEST_STAT) + " " + std::to_string(rank) + " " + path;

        MPI_Send(msg.c_str(), msg.length() + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
        (*pending_remote_stats)[path].emplace_back(tid);
    }

    inline void recv_file(char *shm, int source, long int bytes_expected) override {
        START_LOG(gettid(), "call(%ld, %d, %ld)", shm, source, bytes_expected);
        MPI_Status status;
        int bytes_received, count;

        for (long int k = 0; k < bytes_expected; k += bytes_received) {
            (bytes_expected - k > MPI_MAX_ELEM_COUNT) ? count = MPI_MAX_ELEM_COUNT
                                                      : count = bytes_expected - k;

            MPI_Recv(shm + k, count, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_BYTE, &bytes_received);
        }
    }

}; // namespace backend

#endif // CAPIO_CAPIO_COMMS_H