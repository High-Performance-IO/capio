#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP
#include "capio/env.hpp"
#include "client-manager/client_manager.hpp"
#include "file_manager.hpp"
#include "storage-service/capio_storage_service.hpp"
#include "utils/distributed_semaphore.hpp"

inline std::string CapioFileManager::getMetadataPath(const std::string &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    const std::filesystem::path &metadata_path = get_capio_metadata_path();
    LOG("Obtained metadata token path: %s", metadata_path.c_str());
    const std::filesystem::path token_pathname = path + ".capio";
    LOG("Token name relative to metadata path is %s", token_pathname.c_str());
    const std::filesystem::path token_full_path = metadata_path / token_pathname;
    LOG("Computed token path is: %s", token_full_path.c_str());
    return token_pathname;
}

/**
 * @brief Creates the directory structure for the metadata file and proceed to return the path
 * pointing to the metadata token file. For improvements in performances, a hash map is included to
 * cache the computed paths. For thread safety concerns, see
 * https://en.cppreference.com/w/cpp/container#Thread_safety
 *
 * @param path real path of the file
 * @return std::string with the translated capio token metadata path
 */
inline std::string CapioFileManager::getAndCreateMetadataPath(const std::string &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    static std::unordered_map<std::string, std::string> metadata_paths;
    if (metadata_paths.find(path) == metadata_paths.end()) {
        std::filesystem::path result = getMetadataPath(path);

        metadata_paths.emplace(path, result);
        LOG("Creating metadata directory (%s)", result.parent_path().c_str());
        std::filesystem::create_directories(result.parent_path());
        LOG("Created capio metadata parent path (if no file existed). returning metadata token "
            "file");
    }
    LOG("token_path=%s", metadata_paths[path].c_str());
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
inline void CapioFileManager::addThreadAwaitingCreation(const std::string &path, pid_t tid) {
    START_LOG(gettid(), "call(path=%s, tid=%ld)", path.c_str(), tid);
    const std::lock_guard lg(creation_mutex);
    thread_awaiting_file_creation[path].push_back(tid);
}

/**
 * @brief Awakes all threads waiting for the creation of a file
 *
 * @param path file that has just been created
 * @param pids
 */
inline void CapioFileManager::_unlockThreadAwaitingCreation(const std::string &path,
                                                            const std::vector<pid_t> &pids) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    for (const auto tid : pids) {
        client_manager->reply_to_client(tid, 1);
        /*
         * Here we need to create a new remote file, as it might be that the file is not
         * produced by this node but by another remote one
         */
        storage_service->createRemoteFile(path);
    }
}

/**
 * @brief Register a process waiting on a file to exist and with a file size of at least the
 * expected_size parameter.
 *
 * @param path
 * @param tid
 * @param expected_size
 */
inline void CapioFileManager::addThreadAwaitingData(const std::string &path, int tid,
                                                    size_t expected_size) {
    START_LOG(gettid(), "call(path=%s, tid=%ld, expected_size=%ld)", path.c_str(), tid,
              expected_size);

    // check if file needs to be handled by the storage service instead
    if (capio_cl_engine->storeFileInMemory(path)) {
        LOG("File is stored in memory. delegating storage_service to await for data");
        storage_service->addThreadWaitingForData(tid, path, 0, expected_size);
        return;
    }

    const std::lock_guard lg(data_mutex);
    thread_awaiting_data[path].emplace(tid, expected_size);
}

/**
 * @brief Loop between all thread registered on the file path, and check for each
 * one if enough data has been produced. If so, unlock and remove the thread
 *
 * @param path
 * @param pids_awaiting
 */
inline void CapioFileManager::_unlockThreadAwaitingData(
    const std::string &path, std::unordered_map<pid_t, capio_off64_t> &pids_awaiting) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    for (auto item = pids_awaiting.begin(); item != pids_awaiting.end();) {
        LOG("Handling thread");

        /**
         * if the file is a directory, allow to continue by returning ULLONG_MAX. This behaviour
         * should be triggered only if the file is a directory and the rule specified on it is
         * Fire No Update
         */
        const bool is_directory  = std::filesystem::is_directory(path);
        const uintmax_t filesize = is_directory ? ULLONG_MAX : get_file_size_if_exists(path);
        /*
         * Check for file size only if it is directory, otherwise,
         * return the max allowed size, to allow the process to continue.
         * This is caused by the fact that std::filesystem::file_size is
         * implementation defined when invoked on directories
         */

        if (capio_cl_engine->isProducer(path, item->first)) {
            LOG("Thread %ld can be unlocked as thread is producer", item->first);
            // if producer, return the file as committed to allow to execute operations without
            // being interrupted
            // todo: this might be an issue later. not sure yet
            client_manager->reply_to_client(item->first, ULLONG_MAX);
            // remove thread from map
            LOG("Removing thread %ld from threads awaiting on data", item->first);
            item = pids_awaiting.erase(item);

        } else if (capio_cl_engine->isFirable(path) && filesize >= item->second) {
            /**
             * if is Fire No Update and there is enough data
             */
            LOG("Thread %ld can be unlocked as mode is FNU AND there is enough data  to serve "
                "(%llu bytes available. Requested %llu)",
                item->first, filesize, item->second);
            client_manager->reply_to_client(item->first, filesize);
            // remove thread from map
            LOG("Removing thread %ld from threads awaiting on data", item->first);
            item = item = pids_awaiting.erase(item);

        } else if (isCommitted(path)) {

            LOG("Thread %ld can be unlocked as file is committed", item->first);
            client_manager->reply_to_client(item->first, ULLONG_MAX);
            // remove thread from map
            LOG("Removing thread %ld from threads awaiting on data", item->first);
            item = item = pids_awaiting.erase(item);
        } else {

            // DEFAULT: no condition to unlock has occurred, hence wait...
            LOG("Waiting threads cannot yet be unlocked");
            ++item;
        }
    }

    LOG("Completed loops over threads vector for file!");
}

/**
 * @brief Update the CAPIO metadata n_close option by adding one to the current value
 *
 * @param path
 */
inline void CapioFileManager::increaseCloseCount(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path    = getAndCreateMetadataPath(path);
    auto lock             = new DistributedSemaphore(metadata_path + ".lock", 300);
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
inline void CapioFileManager::setCommitted(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    auto metadata_path = getAndCreateMetadataPath(path);
    LOG("Creating token %s", metadata_path.c_str());
    auto fd = open(metadata_path.c_str(), O_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0664);
    LOG("Token fd = %d", fd);
    close(fd);
}

/**
 * @brief Set all the files that are currently open, or have been open by a given process to be
 * committed
 *
 * @param tid
 */
inline void CapioFileManager::setCommitted(const pid_t tid) {
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
    /**
     * Hash map to store committed files to avoid recomputing the commit state of a given file
     * Files inside here are inserted only when they are committed
     */
    static std::unordered_map<std::string, bool> committed_files;

    if (committed_files.find(path) != committed_files.end()) {
        LOG("Committed: TRUE");
        return true;
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

        LOG("Final result: %s", count >= file_count ? "COMMITTED" : "NOT COMMITTED");

        if (count >= file_count) {
            committed_files[path] = true;
            LOG("Committed: TRUE");
            return true;
        }
        LOG("Committed: FALSE");
        return false;
    }

    // if file
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
        if (commit_computed) {
            committed_files[path] = true;
        }
        LOG("Committed: %s", commit_computed ? "TRUE" : "FALSE");
        return commit_computed;
    }

    if (commit_rule == CAPIO_FILE_COMMITTED_ON_CLOSE) {
        LOG("Commit rule is ON_CLOSE");

        if (!std::filesystem::exists(metadata_computed_path)) {
            LOG("Commit file does not yet exists.");
            LOG("Committed: FALSE");
            return false;
        }

        int commit_count = capio_cl_engine->getCommitCloseCount(path);
        LOG("Expected close count is: %d", commit_count);
        if (commit_count == -1) {
            LOG("File needs to be closed exactly once and token exists. returning");
            LOG("Committed: TRUE");
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

        if (actual_commit_count >= commit_count) {
            committed_files[path] = true;
            return true;
            LOG("Committed: TRUE");
        }
        LOG("Committed: FALSE");
        return false;
    }

    LOG("Commit rule is ON_TERMINATION. File exists? %s",
        std::filesystem::exists(metadata_computed_path) ? "TRUE" : "FALSE");

    if (std::filesystem::exists(metadata_computed_path)) {
        committed_files[path] = true;
        LOG("Committed: TRUE");
        return true;
    }
    LOG("Committed: FALSE");
    return false;
}

/**
 * @brief Check for threads awaiting file creation and unlock threads waiting on them
 *
 */
inline void CapioFileManager::checkFilesAwaitingCreation() {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    const std::lock_guard lg(creation_mutex);
    std::vector<std::string> path_to_delete;

    for (auto element : thread_awaiting_file_creation) {
        if (std::filesystem::exists(element.first)) {
            START_LOG(gettid(), "\n\ncall()");
            LOG("File %s exists. Unlocking thread awaiting for creation", element.first.c_str());
            CapioFileManager::_unlockThreadAwaitingCreation(element.first, element.second);
            LOG("Completed handling.");
            path_to_delete.push_back(element.first);
        }
    }

    for (auto path : path_to_delete) {
        thread_awaiting_file_creation.erase(path);
    }
}

/**
 * @brief check if there are threads waiting for data, and for each one of them check if the file
 * has enough data
 *
 */
inline void CapioFileManager::checkFileAwaitingData() {
    // NOTE: do not put inside here log code as it will generate a lot of useless log
    const std::lock_guard lg(data_mutex);
    for (auto iter = thread_awaiting_data.begin(); iter != thread_awaiting_data.end();) {
        START_LOG(gettid(), "\n\ncall()");
        // no need to check if file exists as this method is called only by read_handler
        // and as such, the file already exists
        // actual update, end eventual removal from map is handled by the
        // CapioFileManager class and not by the FileSystemMonitor class
        _unlockThreadAwaitingData(iter->first, iter->second);

        // cleanup of map while iterating over it
        if (iter->second.empty()) {
            LOG("There are no threads waiting for path %s. cleaning up map", iter->first.c_str());
            iter = thread_awaiting_data.erase(iter);
        } else {
            LOG("There are threads waiting for path %s. SKIPPING CLEANUP", iter->first.c_str());
            ++iter;
        }
        LOG("Completed handling.");
    }
}

/**
 * @brief commit directories that have NFILES inside them if their commit rule is n_files
 */
inline void CapioFileManager::checkDirectoriesNFiles() const {
    /*
     * WARN: this function directly access the _location internal structure in read only mode to
     * avoid race conditions. Since we do not update locations, get the pointer only at the
     * beginning and then use it.
     */
    static const auto loc = capio_cl_engine->getLocations();

    for (const auto &[path_config, config] : *loc) {

        if (std::get<6>(config)) {
            /*
             * In this case we are trying to check for a file.
             * skip this check and go to the next path.
             */
            continue;
        }

        if (auto n_files = std::get<8>(config); n_files > 0) {
            START_LOG(gettid(), "call()");
            LOG("Directory %s needs %ld files before being committed", path_config.c_str(),
                n_files);
            // There must be n_files inside the directory to commit the file
            long count = 0;
            if (std::filesystem::exists(path_config)) {
                auto iterator = std::filesystem::directory_iterator(path_config);
                for (const auto &entry : iterator) {
                    ++count;
                }
            }

            LOG("Directory %s has %ld files inside", path_config.c_str(), count);
            if (count >= n_files) {
                LOG("Committing directory");
                this->setCommitted(path_config);
            }
        }
    }
}

#endif // FILE_MANAGER_HPP