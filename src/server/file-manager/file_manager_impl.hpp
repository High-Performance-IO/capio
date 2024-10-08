#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP
#include "capio/env.hpp"
#include "client-manager/client_manager.hpp"
#include "file_manager.hpp"
#include "utils/distributed_semaphore.hpp"

/**
 * @brief Creates the directory structure for the metadata file and proceed to return the path
 * pointing to the metadata token file. For improvements in performances, a hash map is included to
 * cache the computed paths. For thread safety conserns, see
 * https://en.cppreference.com/w/cpp/container#Thread_safety
 *
 * @param path real path of the file
 * @return std::string with the translated capio token metadata path
 */
inline std::string CapioFileManager::getAndCreateMetadataPath(const std::string &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    static std::unordered_map<std::string, std::string> metadata_paths;
    if (metadata_paths.find(path) == metadata_paths.end()) {
        std::filesystem::path result =
            get_capio_metadata_path() / (path.substr(path.find(get_capio_dir()) + 1) + ".capio");
        metadata_paths.emplace(path, result);
        LOG("Creating metadata directory (%s)", result.parent_path().c_str());
        std::filesystem::create_directories(result.parent_path());
        LOG("Created capio metadata parent path (if no file existed). returning metadata token "
            "file");
    }
    return metadata_paths[path];
}

/**
 * @brief Get the file size
 *
 * @param path
 * @return uintmax_t file size if file exists, 0 otherwise
 */
inline uintmax_t CapioFileManager::get_file_size_if_exists(const std::filesystem::path &path) {
    return std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
}

/**
 * @brief Register a thread to the threads waiting for a file to exists (inside the CapioFSMonitor)
 * for a given file path to exists
 *
 * @param path
 * @param tid
 */
inline void CapioFileManager::addThreadAwaitingCreation(std::string path, pid_t tid) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld)", path.c_str(), tid);
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->try_emplace(path, new std::vector<int>);
    thread_awaiting_file_creation->at(path)->emplace_back(tid);
}

/**
 * @brief Awakes all threads waiting for the creation of a file
 *
 * @param path file that has just been created
 */
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

/**
 * @brief Remove a file from the list of files for which at least a thread is waiting. if some
 * thread are still waiting they will be removed and hence go into deadlock indefinitely. This
 * method should only be called after awaking all waiting threads
 *
 * @param path
 */
inline void CapioFileManager::deleteFileAwaitingCreation(std::string path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::lock_guard<std::mutex> lg(threads_mutex);
    thread_awaiting_file_creation->erase(path);
}

/**
 * @brief Register a process waiting on a file to exist and with a file size of at least the
 * expected_size parameter.
 *
 * @param path
 * @param tid
 * @param expected_size
 */
inline void CapioFileManager::addThreadAwaitingData(std::string path, int tid,
                                                    size_t expected_size) const {
    START_LOG(gettid(), "call(path=%s, tid=%ld, expected_size=%ld)", path.c_str(), tid,
              expected_size);
    std::lock_guard<std::mutex> lg(data_mutex);
    thread_awaiting_data->try_emplace(path, new std::unordered_map<pid_t, capio_off64_t>);
    thread_awaiting_data->at(path)->emplace(tid, expected_size);
}

/**
 * @brief Loop between all thread registered on the file path, and check for each
 * one if enough data has been produced. If so, unlock and remove the thread
 *
 * @param path
 */
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

            const bool committed       = isCommitted(path);
            const bool file_size_check = item->first >= filesize;
            const bool is_fnu = capio_cl_engine->getFireRule(path) == CAPIO_FILE_MODE_NO_UPDATE;
            const bool is_producer    = capio_cl_engine->isProducer(path, item->first);
            const bool lock_condition = committed || is_producer || (file_size_check && is_fnu);

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

/**
 * @brief Update the CAPIO metadata n_close option by adding one to the current value
 *
 * @param path
 */
inline void CapioFileManager::increaseCloseCount(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path    = getAndCreateMetadataPath(path);
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

/**
 * @brief Set a CAPIO handled file to be committed
 *
 * @param path
 */
inline void CapioFileManager::setCommitted(const std::filesystem::path &path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path = getAndCreateMetadataPath(path);
    LOG("Creating token %s", metadata_path.c_str());
    auto fd = open(metadata_path.c_str(), O_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0664);
    LOG("Token fd = %d", fd);
    close(fd);
    checkAndUnlockThreadAwaitingData(path);
    unlockThreadAwaitingCreation(path);
}

/**
 * @brief Set all the files that are currently open, or have been open by a given process to be
 * committed
 *
 * @param tid
 */
inline void CapioFileManager::setCommitted(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%d)", tid);
    auto files = client_manager->get_produced_files(tid);
    for (const auto &file : *files) {
        LOG("Committing file %s", file.c_str());
        CapioFileManager::setCommitted(file);
    }
}

/**
 * @brief Returns whether the file is committed or not
 *
 * @param path
 * @return true if is committed
 * @return false if it is not
 */
inline bool CapioFileManager::isCommitted(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

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
    }

    // if is file
    LOG("Path is a file");
    std::string metadata_computed_path = getAndCreateMetadataPath(path);
    LOG("Computed metadata file path is %s", metadata_computed_path.c_str());

    std::string commit_rule = capio_cl_engine->getCommitRule(path);

    if (commit_rule == CAPIO_FILE_COMMITTED_ON_FILE) {
        LOG("Commit rule is on_file. Checking for file dependencies");
        bool commit_computed = true;
        for (const auto &file : capio_cl_engine->get_file_deps(path)) {
            commit_computed = commit_computed && isCommitted(file);
        }

        LOG("Commit result for file %s is: %s", path.c_str(),
            commit_computed ? "committed" : "not committed");

        return commit_computed;
    }

    if (commit_rule == CAPIO_FILE_COMMITTED_ON_CLOSE) {
        LOG("Commit rule is ON_CLOSE");

        if (!std::filesystem::exists(metadata_computed_path)) {
            LOG("Commit file does not yet exists. returning false");
            return false;
        }

        int commit_count = capio_cl_engine->getCommitCloseCount(path);
        LOG("Expected close count is: %d", commit_count);
        if (commit_count == -1) {
            LOG("File needs to be closed exactly once and token exists. returning");
            return true;
        }

        long actual_commit_count = 0;
        LOG("Commit file exists. retrieving commit count");
        std::ifstream in(metadata_computed_path);
        if (in.is_open()) {
            LOG("Opened file");
            in >> actual_commit_count;
        }
        LOG("Obtained actual commit count: %l", actual_commit_count);
        LOG("File %s committed", actual_commit_count >= commit_count ? "IS" : "IS NOT");
        return actual_commit_count >= commit_count;
    }

    LOG("Commit rule is ON_TERMINATION. File exists? %s",
        std::filesystem::exists(metadata_computed_path) ? "TRUE" : "FALSE");
    return std::filesystem::exists(metadata_computed_path);
}

/**
 * @brief Return the files that have at least one process waiting for its creation
 *
 * @return std::vector<std::string>
 */
inline std::vector<std::string> CapioFileManager::getFileAwaitingCreation() const {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    std::lock_guard<std::mutex> lg(threads_mutex);
    std::vector<std::string> keys;
    for (auto itm : *thread_awaiting_file_creation) {
        keys.emplace_back(itm.first);
    }
    return keys;
}

/**
 * @brief Return a list of files for which there is a thread waiting for data to be produced
 *
 * @return std::vector<std::string>
 */
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
