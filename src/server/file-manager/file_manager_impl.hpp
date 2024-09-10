#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP
#include "client-manager/client_manager.hpp"
#include "file_manager.hpp"

inline void CapioFileManager::add_thread_awaiting_creation(std::string path, pid_t tid) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld)", path.c_str(), tid);
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->try_emplace(path, new std::vector<int>);
    thread_awaiting_file_creation->at(path)->emplace_back(tid);
}

inline void CapioFileManager::unlock_thread_awaiting_creation(std::string path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::lock_guard<std::mutex> lg(threads_mutex);
    if (thread_awaiting_file_creation->find(path) != thread_awaiting_file_creation->end()) {
        auto th = thread_awaiting_file_creation->at(path);
        for (auto tid : *th) {
            client_manager->reply_to_client(tid, 1);
        }
    }
    thread_awaiting_file_creation->erase(path);
}

inline void CapioFileManager::delete_file_awaiting_creation(std::string path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->erase(path);
}

// register tid to wait for file size of certain size
inline void CapioFileManager::add_thread_awaiting_data(std::string path, int tid,
                                                       size_t expected_size) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld, expected_size=%ld)", path.c_str(), tid,
              expected_size);
    std::lock_guard<std::mutex> lg(data_mutex);
    thread_awaiting_data->try_emplace(path, new std::unordered_map<pid_t, capio_off64_t>);
    thread_awaiting_data->at(path)->emplace(tid, expected_size);
}

inline void CapioFileManager::check_and_unlock_thread_awaiting_data(std::string path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto path_size = std::filesystem::file_size(path);
    std::lock_guard<std::mutex> lg(data_mutex);
    if (thread_awaiting_data->find(path) != thread_awaiting_data->end()) {
        LOG("Path has thread awaiting");
        auto threads = thread_awaiting_data->at(path);

        for (auto item = threads->begin(); item != threads->end();) {
            LOG("Handling thread");
            if (is_committed(path) || item->first >= std::filesystem::file_size(path)) {
                LOG("Thread %ld can be unlocked", item->first);
                client_manager->reply_to_client(item->first, path_size);
                // remove thread from map
                LOG("Removing thread %ld from threads awaiting on data", item->first);
                item = threads->erase(item);
            } else {
                ++item;
            }
        }

        if (threads->empty()) {
            LOG("There are no threads waiting for path %s. cleaning up map", path.c_str());
            thread_awaiting_data->erase(path);
        }
    }
}

inline void CapioFileManager::set_committed(const std::filesystem::path &path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path = path.string() + ".capio";
    LOG("Creating token %s", metadata_path.c_str());
    auto fd = creat(metadata_path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    LOG("Token fd = %d", fd);
    close(fd);
    check_and_unlock_thread_awaiting_data(path);
    unlock_thread_awaiting_creation(path);
}

inline void CapioFileManager::set_committed(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%d)", tid);
    auto files = client_manager->get_produced_files(tid);
    for (const auto &file : *files) {
        LOG("Committing file %s", file.c_str());
        CapioFileManager::set_committed(file);
    }
}

inline bool CapioFileManager::is_committed(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::string computed_path = path.string() + ".capio";
    LOG("File %s %s committed", path.c_str(),
        std::filesystem::exists(computed_path) ? "is" : "is not");
    return std::filesystem::exists(computed_path);
}

inline std::vector<std::string> CapioFileManager::get_file_awaiting_creation() const {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    std::lock_guard<std::mutex> lg(threads_mutex);
    std::vector<std::string> keys;
    for (auto itm : *thread_awaiting_file_creation) {
        keys.emplace_back(itm.first);
    }
    return keys;
}

inline std::vector<std::string> CapioFileManager::get_file_awaiting_data() const {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    std::lock_guard<std::mutex> lg(data_mutex);
    std::vector<std::string> keys;
    for (auto itm : *thread_awaiting_data) {
        keys.emplace_back(itm.first);
    }
    return keys;
}

#endif // FILE_MANAGER_HPP
