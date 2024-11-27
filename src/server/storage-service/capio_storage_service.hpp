#ifndef CAPIO_STORAGE_SERVICE_H
#define CAPIO_STORAGE_SERVICE_H

#include "../../posix/utils/env.hpp"
#include "CapioFile/CapioFile.hpp"
#include "CapioFile/CapioMemoryFile.hpp"

class CapioStorageService {

    std::unordered_map<pid_t, SPSCQueue *> *_client_to_server_queue;
    std::unordered_map<pid_t, SPSCQueue *> *_server_to_clien_queue;
    std::unordered_map<std::string, CapioFile *> *_stored_files;

  public:
    CapioStorageService() {
        START_LOG(gettid(), "call()");
        _stored_files           = new std::unordered_map<std::string, CapioFile *>;
        _client_to_server_queue = new std::unordered_map<pid_t, SPSCQueue *>;
        _server_to_clien_queue  = new std::unordered_map<pid_t, SPSCQueue *>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioStorageService initialization completed." << std::endl;
    }

    ~CapioStorageService() {
        delete _stored_files;
        delete _client_to_server_queue;
        delete _server_to_clien_queue;
    }

    void createFile(const std::string &file_name) const {
        _stored_files->emplace(file_name, new CapioMemoryFile(file_name));
    }

    void deleteFile(const std::string &file_name) const { _stored_files->erase(file_name); }

    [[nodiscard]] auto getFile(const std::string &file_name) const {
        if (_stored_files->find(file_name) == _stored_files->end()) {
            createFile(file_name);
            return _stored_files->at(file_name);
        }
        return _stored_files->at(file_name);
    }

    /**
     * Notify the occurrence of an operation on a given file, for which other servers running at a
     * certain point might be required to know. This function is used to allow CAPIO to function in
     * the event that a distributed file system (or at least a shared directory) is not available
     */
    void notifyEvent(const std::string &event_name, const std::filesystem::path &filename) const {
        // TODO: implement this
    }

    void register_client(const std::string &app_name, const pid_t pid) const {
        START_LOG(gettid(), "call(app_name=%s)", app_name.c_str());
        auto cts_queue = new SPSCQueue("queue-" + std::to_string(pid) + +".cts",
                                       CAPIO_MAX_SPSQUEUE_ELEMS, CAPIO_MAX_SPSCQUEUE_ELEM_SIZE);
        auto stc_queue = new SPSCQueue("queue-" + std::to_string(pid) + +".stc",
                                       CAPIO_MAX_SPSQUEUE_ELEMS, CAPIO_MAX_SPSCQUEUE_ELEM_SIZE);
        _client_to_server_queue->emplace(pid, cts_queue);
        _server_to_clien_queue->emplace(pid, stc_queue);
        LOG("Created communication queues");
    }

    void remove_client(const pid_t pid) const {
        _client_to_server_queue->erase(pid);
        _server_to_clien_queue->erase(pid);
    }

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
