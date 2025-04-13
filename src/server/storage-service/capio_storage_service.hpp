#ifndef CAPIO_STORAGE_SERVICE_H
#define CAPIO_STORAGE_SERVICE_H

#include "CapioFile/CapioFile.hpp"
#include "CapioFile/CapioMemoryFile.hpp"

class CapioStorageService {

    // TODO: put all of this conde on a different thread

    std::unordered_map<pid_t, SPSCQueue *> *_client_to_server_queue;
    std::unordered_map<pid_t, SPSCQueue *> *_server_to_clien_queue;
    std::unordered_map<std::string, CapioFile *> *_stored_files;

    std::unordered_map<std::string, std::vector<std::tuple<capio_off64_t, capio_off64_t, pid_t>>>
        *_threads_waiting_for_memory_data;

    /**
     * Return a file if exists. if not, create it and then return it
     * @param file_name
     * @return
     */
    [[nodiscard]] auto getFile(const std::string &file_name) const {
        if (_stored_files->find(file_name) == _stored_files->end()) {
            createFile(file_name);
        }
        return _stored_files->at(file_name);
    }

  public:
    CapioStorageService() {
        START_LOG(gettid(), "call()");
        _stored_files           = new std::unordered_map<std::string, CapioFile *>;
        _client_to_server_queue = new std::unordered_map<pid_t, SPSCQueue *>;
        _server_to_clien_queue  = new std::unordered_map<pid_t, SPSCQueue *>;
        _threads_waiting_for_memory_data =
            new std::unordered_map<std::string,
                                   std::vector<std::tuple<capio_off64_t, capio_off64_t, pid_t>>>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioStorageService initialization completed." << std::endl;
    }

    ~CapioStorageService() {
        // TODO: dump files to FS
        delete _stored_files;
        delete _client_to_server_queue;
        delete _server_to_clien_queue;
        delete _threads_waiting_for_memory_data;
    }

    void createFile(const std::string &file_name) const {
        // TODO: understand when is local or remte CapioFile
        _stored_files->emplace(file_name, new CapioMemoryFile(file_name));
    }

    void deleteFile(const std::string &file_name) const { _stored_files->erase(file_name); }

    /**
     * Notify the occurrence of an operation on a given file, for which other servers running at a
     * certain point might be required to know. This function is used to allow CAPIO to function in
     * the event that a distributed file system (or at least a shared directory) is not available
     */
    void notifyEvent(const std::string &event_name, const std::filesystem::path &filename) const {
        // TODO: implement this
    }

    /**
     * Add a new thread in the list of thhreads awaiting for expected_size to be available
     * @param tid
     * @param path
     * @param offset
     * @param  size
     */
    void addThreadWaitingForData(pid_t tid, const std::string &path, capio_off64_t offset,
                                 capio_off64_t size) const {
        START_LOG(gettid(), "call(tid=%d, path=%s, offset=%lld, size=%lld)", tid, path.c_str(),
                  offset, size);
        if (_threads_waiting_for_memory_data->find(path) ==
            _threads_waiting_for_memory_data->end()) {
            _threads_waiting_for_memory_data->insert({path, {}});
        }

        _threads_waiting_for_memory_data->at(path).emplace_back(std::make_tuple(offset, size, tid));
    }

    void check_and_unlock_thread_awaiting_data(const std::string &path) {
        auto threads = _threads_waiting_for_memory_data->at(path);

        auto file_size = sizeOf(path);

        for (const auto [offset, size, thread_id] : threads) {
            if (file_size >= offset + size) {
                reply_to_client(thread_id, path, offset, size);
            }
        }
    }

    /**
     * Return size of given path as contained inside memory
     * @param path
     * @return
     */
    size_t sizeOf(const std::string &path) const {
        START_LOG(gettid(), "call(file=%s)", path.c_str());
        return getFile(path)->getSize();
    }

    /**
     * Initialize a new client data structure
     * @param app_name
     * @param pid
     */
    void register_client(const std::string &app_name, const pid_t pid) const {
        START_LOG(gettid(), "call(app_name=%s)", app_name.c_str());
        _client_to_server_queue->emplace(
            pid,
            new SPSCQueue("queue-" + std::to_string(pid) + +".cts", CAPIO_MAX_SPSQUEUE_ELEMS,
                          CAPIO_MAX_SPSCQUEUE_ELEM_SIZE, capio_config->CAPIO_WORKFLOW_NAME, false));
        _server_to_clien_queue->emplace(
            pid,
            new SPSCQueue("queue-" + std::to_string(pid) + +".stc", CAPIO_MAX_SPSQUEUE_ELEMS,
                          CAPIO_MAX_SPSCQUEUE_ELEM_SIZE, capio_config->CAPIO_WORKFLOW_NAME, false));
        LOG("Created communication queues");
    }

    /**
     * Send the file content to a client application
     * @param pid
     * @param file
     * @param offset
     * @param size
     */
    void reply_to_client(pid_t pid, const std::string &file, capio_off64_t offset,
                         capio_off64_t size) const {
        START_LOG(gettid(), "call(pid=%llu, file=%s, offset=%llu, size=%llu)", pid, file.c_str(),
                  offset, size);

        getFile(file)->writeToQueue(*_server_to_clien_queue->at(pid), offset, size);
    }

    /**
     * Receive the file content from the client application
     * @param tid
     * @param file
     * @param offset
     * @param size
     */
    void recive_from_client(pid_t tid, const std::string &file, capio_off64_t offset,
                            off64_t size) const {
        START_LOG(gettid(), "call(tid=%d, file=%s, offset=%lld, size=%lld)", tid, file.c_str(),
                  offset, size);

        getFile(file)->readFromQueue(*_client_to_server_queue->at(tid), offset, size);
    }

    void remove_client(const pid_t pid) const {
        _client_to_server_queue->erase(pid);
        _server_to_clien_queue->erase(pid);
    }

    /**
     * Return a list of regex used to match files that need to be stored inside memory first
     * to a posix application
     * @param pid
     * @return
     */
    [[nodiscard]] size_t sendFilesToStoreInMemory(const long pid) const {
        START_LOG(gettid(), "call(pid=%d)", pid);
        auto files_to_store_in_mem = capio_cl_engine->getFileToStoreInMemory();
        for (const auto &file : files_to_store_in_mem) {
            LOG("Sending file %s", file.c_str());
            _server_to_clien_queue->at(pid)->write(file.c_str());
        }

        LOG("Return value=%llu", files_to_store_in_mem.size());
        return files_to_store_in_mem.size();
    }
};

inline CapioStorageService *storage_service;

#endif // CAPIO_STORAGE_SERVICE_H
