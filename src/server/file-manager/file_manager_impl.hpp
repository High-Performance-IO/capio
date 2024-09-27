#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP
#include "client-manager/client_manager.hpp"
#include "file_manager.hpp"
#include "utils/distributed_semaphore.hpp"

inline uintmax_t get_file_size_if_exists(const std::filesystem::path &path) {
    if (std::filesystem::exists(path)) {
        return std::filesystem::file_size(path);
    }
    return 0;
}

inline void CapioFileManager::addThreadAwaitingCreation(std::string path, pid_t tid) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld)", path.c_str(), tid);
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->try_emplace(path, new std::vector<int>);
    thread_awaiting_file_creation->at(path)->emplace_back(tid);
}

inline void CapioFileManager::unlockThreadAwaitingCreation(std::string path) const {
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

inline void CapioFileManager::deleteFileAwaitingCreation(std::string path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->erase(path);
}

// register tid to wait for file size of certain size
inline void CapioFileManager::addThreadAwaitingData(std::string path, int tid,
                                                    size_t expected_size) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld, expected_size=%ld)", path.c_str(), tid,
              expected_size);
    std::lock_guard<std::mutex> lg(data_mutex);
    thread_awaiting_data->try_emplace(path, new std::unordered_map<pid_t, capio_off64_t>);
    thread_awaiting_data->at(path)->emplace(tid, expected_size);
}

// TODO:
inline void CapioFileManager::checkAndUnlockThreadAwaitingData(const std::string &path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    LOG("Before lockguard");
    std::lock_guard<std::mutex> lg(data_mutex);
    LOG("Acquired lockguard");
    if (thread_awaiting_data->find(path) != thread_awaiting_data->end()) {
        LOG("Path has thread awaiting");
        auto threads = thread_awaiting_data->at(path);
        LOG("Obtained threads");
        for (auto item = threads->begin(); item != threads->end();) {
            LOG("Handling thread");
            uintmax_t filesize = -1;

            filesize = std::filesystem::is_directory(path) ? -1 : get_file_size_if_exists(path);

            bool committed       = isCommitted(path);
            bool file_size_check = item->first >= filesize;
            bool is_fnu          = capio_cl_engine->getFireRule(path) == CAPIO_FILE_MODE_NO_UPDATE;
            bool is_producer     = capio_cl_engine->isProducer(path, item->first);
            bool lock_condition  = committed || is_producer || (file_size_check && is_fnu);

            LOG("( committed(%s) || is_producer(%s) ||  ( file_size_check(%s) && is_fnu(%s) ) )",
                committed ? "true" : "false", is_producer ? "true" : "false",
                file_size_check ? "true" : "false", is_fnu ? "true" : "false");

            LOG("Evaluation of expression: %s", lock_condition ? "TRUE" : "FALSE");

            if (lock_condition) {
                LOG("Thread %ld can be unlocked", item->first);
                /*
                 * Check for file size only if it is directory, otherwise,
                 * return the max allowed size, to allow the process to continue.
                 * This is caused by the fact that std::filesystem::file_size is
                 * implementation defined when invoked on directories
                 */
                client_manager->reply_to_client(item->first, std::filesystem::is_directory(path)
                                                                 ? ULLONG_MAX
                                                                 : get_file_size_if_exists(path));
                // remove thread from map
                LOG("Removing thread %ld from threads awaiting on data", item->first);
                item = threads->erase(item);
            } else {
                LOG("Waiting threads cannot yet be unlocked");
                ++item;
            }
        }

        LOG("Completed loops over threads vector for file!");

        if (threads->empty()) {
            LOG("There are no threads waiting for path %s. cleaning up map", path.c_str());
            thread_awaiting_data->erase(path);
        }
        LOG("Completed checks");
    }
}

inline void CapioFileManager::increaseCloseCount(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path    = path.string() + ".capio";
    const auto lock       = new DistributedSemaphore(metadata_path + ".lock", 300);
    long long close_count = 0;
    LOG("Gained mutual exclusive access to token file %s", (metadata_path + ".lock").c_str());

    std::fstream f(metadata_path, std::ios::in | std::ios::out);
    LOG("Opened CAPIO metadata file %s", metadata_path.c_str());
    f >> close_count;
    LOG("Close count is %llu. Increasing by one", close_count);
    close_count++;
    f.close();

    auto out = fopen(metadata_path.c_str(), "w");
    fprintf(out, " %llu \n", close_count);
    fclose(out);

    LOG("Updated close count to %llu", close_count);

    delete lock;
}

inline void CapioFileManager::setCommitted(const std::filesystem::path &path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path = path.string() + ".capio";
    LOG("Creating token %s", metadata_path.c_str());
    auto fd = open(metadata_path.c_str(), O_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0664);
    LOG("Token fd = %d", fd);
    close(fd);
    checkAndUnlockThreadAwaitingData(path);
    unlockThreadAwaitingCreation(path);
}

inline void CapioFileManager::setCommitted(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%d)", tid);
    auto files = client_manager->get_produced_files(tid);
    for (const auto &file : *files) {
        LOG("Committing file %s", file.c_str());
        CapioFileManager::setCommitted(file);
    }
}

inline bool CapioFileManager::isCommitted(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    bool file_exists = std::filesystem::exists(path);
    LOG("File %s exists: %s", path.c_str(), file_exists ? "true" : "false");

    if (!file_exists) {
        LOG("File %s does not yet exists", path.c_str());
        return false;
    }

    if (std::filesystem::is_directory(path)) {
        // is directory
        // check for n_files inside a directory
        LOG("Path is a directory");
        auto file_count = capio_cl_engine->getDirectoryFileCount(path);
        LOG("Expected file count is %ld", file_count);
        long count = 0;
        for (auto const &file : std::filesystem::directory_iterator{path}) {
            if (file.path().extension() != ".capio") {
                ++count;
            }
        }
        bool continue_directory = count >= file_count;

        LOG("Final result: %s", continue_directory ? "COMMITTED" : "NOT COMMITTED");
        return continue_directory;
    } else {
        // if is file
        LOG("Path is a file");
        std::string computed_path = path.string() + ".capio";
        LOG("CAPIO token file %s %s existing", computed_path.c_str(),
            std::filesystem::exists(computed_path) ? "is" : "is not");

        std::string commit_rule = capio_cl_engine->getCommitRule(path);

        if (commit_rule == CAPIO_FILE_COMMITTED_ON_FILE) {
            LOG("Commit rule is on_file. Checking for file dependencies");
            bool commit_computed = true;
            for (auto file : capio_cl_engine->get_file_deps(path)) {
                commit_computed = commit_computed && isCommitted(file);
            }

            LOG("Commit result for file %s is: %s", computed_path.c_str(),
                commit_computed ? "committed" : "not committed");
            return commit_computed;
        }

        if (commit_rule == CAPIO_FILE_COMMITTED_ON_CLOSE) {
            LOG("Commit rule is ON_CLOSE");
            int commit_count = capio_cl_engine->getCommitCloseCount(path);
            LOG("Expected close count is: %d", commit_count);
            long actual_commit_count = 0;

            if (std::filesystem::exists(path.string() + ".capio")) {
                if (commit_count != -1) {
                    LOG("Commit file exists. retrieving commit count");
                    std::ifstream in(path.string() + ".capio");
                    if (in.is_open()) {
                        LOG("Opened file");
                        in >> actual_commit_count;
                    }
                    LOG("Obtained actual commit count: %l", actual_commit_count);
                    LOG("File %s committed", actual_commit_count >= commit_count ? "IS" : "IS NOT");
                    return actual_commit_count >= commit_count;
                }
                LOG("File needs to be closed exactly once. checking for token existence");
                return std::filesystem::exists(path.string() + ".capio");
            }
        }

        LOG("Commit rule is ON_TERMINATION. File exists? %s",
            std::filesystem::exists(computed_path) ? "TRUE" : "FALSE");
        return std::filesystem::exists(computed_path);
    }
}

inline std::vector<std::string> CapioFileManager::getFileAwaitingCreation() const {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    std::lock_guard<std::mutex> lg(threads_mutex);
    std::vector<std::string> keys;
    for (auto itm : *thread_awaiting_file_creation) {
        keys.emplace_back(itm.first);
    }
    return keys;
}

inline std::vector<std::string> CapioFileManager::getFileAwaitingData() const {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    std::lock_guard<std::mutex> lg(data_mutex);
    std::vector<std::string> keys;
    for (auto itm : *thread_awaiting_data) {
        keys.emplace_back(itm.first);
    }
    return keys;
}

#endif // FILE_MANAGER_HPP
