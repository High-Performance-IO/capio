

#include <capio/env.hpp>
#include <capio/queue.hpp>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/storage-service/capio_file.hpp>
#include <include/storage-service/capio_storage_service.hpp>

auto CapioStorageService::getFile(const std::string &file_name) const {
    if (_stored_files->find(file_name) == _stored_files->end()) {
        createMemoryFile(file_name);
    }
    return _stored_files->at(file_name);
}

CapioStorageService::CapioStorageService() {
    START_LOG(gettid(), "call()");
    _stored_files           = new std::unordered_map<std::string, CapioFile *>;
    _client_to_server_queue = new std::unordered_map<pid_t, SPSCQueue *>;
    _server_to_client_queue = new std::unordered_map<pid_t, SPSCQueue *>;
    _threads_waiting_for_memory_data =
        new std::unordered_map<std::string,
                               std::vector<std::tuple<capio_off64_t, capio_off64_t, pid_t>>>;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioStorageService initialization completed.");
}

CapioStorageService::~CapioStorageService() {
    // TODO: dump files to FS
    delete _stored_files;
    delete _client_to_server_queue;
    delete _server_to_client_queue;
    delete _threads_waiting_for_memory_data;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioStorageService cleanup completed.");
}

void CapioStorageService::createMemoryFile(const std::string &file_name) const {
    _stored_files->emplace(file_name, new CapioMemoryFile(file_name));
}

void CapioStorageService::createRemoteFile(const std::string &file_name,
                                           const std::string &home_node) const {
    /*
     * First we check that the file associate does not yet exists, as it might be produced
     * by another app running under the same server instance. if it is not found, we create
     * the file
     */
    START_LOG(gettid(), "call(file_name=%s)", file_name.c_str());
    if (_stored_files->find(file_name) == _stored_files->end()) {
        LOG("File not found. Creating a new remote file");
        _stored_files->emplace(file_name, new CapioRemoteFile(file_name, home_node));
    }
}

void CapioStorageService::deleteFile(const std::string &file_name) const {
    _stored_files->erase(file_name);
}

void CapioStorageService::notifyEvent(const std::string &event_name,
                                      const std::filesystem::path &filename) const {
    // TODO: implement this
}

void CapioStorageService::addThreadWaitingForData(pid_t tid, const std::string &path,
                                                  capio_off64_t offset, capio_off64_t size) const {
    START_LOG(gettid(), "call(tid=%d, path=%s, offset=%lld, size=%lld)", tid, path.c_str(), offset,
              size);
    if (_threads_waiting_for_memory_data->find(path) == _threads_waiting_for_memory_data->end()) {
        _threads_waiting_for_memory_data->insert({path, {}});
    }

    _threads_waiting_for_memory_data->at(path).emplace_back(std::make_tuple(offset, size, tid));
}

void CapioStorageService::check_and_unlock_thread_awaiting_data(const std::string &path) {
    auto threads = _threads_waiting_for_memory_data->at(path);

    auto file_size = sizeOf(path);

    for (auto &[offset, size, thread_id] : threads) {
        if (file_size >= offset + size) {
            reply_to_client(thread_id, path, offset, size);
        }
    }
}

size_t CapioStorageService::sizeOf(const std::string &path) const {
    START_LOG(gettid(), "call(file=%s)", path.c_str());
    return getFile(path)->getSize();
}

void CapioStorageService::register_client(const std::string &app_name, const pid_t pid) const {
    START_LOG(gettid(), "call(app_name=%s)", app_name.c_str());
    _client_to_server_queue->emplace(pid, new SPSCQueue("queue-" + std::to_string(pid) + +".cts",
                                                        get_cache_lines(), get_cache_line_size(),
                                                        capio_global_configuration->workflow_name,
                                                        false));
    _server_to_client_queue->emplace(pid, new SPSCQueue("queue-" + std::to_string(pid) + +".stc",
                                                        get_cache_lines(), get_cache_line_size(),
                                                        capio_global_configuration->workflow_name,
                                                        false));
    LOG("Created communication queues");
}

size_t CapioStorageService::reply_to_client(pid_t pid, const std::string &file, capio_off64_t offset,
                                          capio_off64_t size) const {
    START_LOG(gettid(), "call(pid=%llu, file=%s, offset=%llu, size=%llu)", pid, file.c_str(),
              offset, size);

    return getFile(file)->writeToQueue(*_server_to_client_queue->at(pid), offset, size);
}

void CapioStorageService::reply_to_client_raw(pid_t pid, const char *data,
                                              const capio_off64_t len) const {
    _server_to_client_queue->at(pid)->write(data, len);
}

void CapioStorageService::recive_from_client(pid_t tid, const std::string &file,
                                             capio_off64_t offset, off64_t size) const {
    START_LOG(gettid(), "call(tid=%d, file=%s, offset=%lld, size=%lld)", tid, file.c_str(), offset,
              size);
    const auto f     = getFile(file);
    const auto queue = _client_to_server_queue->at(tid);
    f->readFromQueue(*queue, offset, size);
}

void CapioStorageService::remove_client(const pid_t pid) const {
    _client_to_server_queue->erase(pid);
    _server_to_client_queue->erase(pid);
}

size_t CapioStorageService::sendFilesToStoreInMemory(const long pid) const {
    START_LOG(gettid(), "call(pid=%d)", pid);

    if (capio_global_configuration->StoreOnlyInMemory) {
        LOG("All files should be handled in memory. sending * wildcard");
        char f[PATH_MAX + 1]{0};
        f[0] = '*';
        _server_to_client_queue->at(pid)->write(f, PATH_MAX);
        LOG("Return value=%llu", 1);
        return 1;
    }

    auto files_to_store_in_mem = capio_cl_engine->getFileToStoreInMemory();
    for (const auto &file : files_to_store_in_mem) {
        LOG("Sending file %s", file.c_str());
        char f[PATH_MAX + 1]{0};
        memcpy(f, file.c_str(), file.size());
        _server_to_client_queue->at(pid)->write(f, PATH_MAX);
    }

    LOG("Return value=%llu", files_to_store_in_mem.size());
    return files_to_store_in_mem.size();
}
