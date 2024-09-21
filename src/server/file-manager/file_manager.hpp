#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

#include <mutex>
std::mutex threads_mutex;
std::mutex data_mutex;

class CapioFileManager {
    std::unordered_map<std::string, std::vector<pid_t> *> *thread_awaiting_file_creation;
    std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>
        *thread_awaiting_data;

  public:
    CapioFileManager() {
        START_LOG(gettid(), "call()");
        thread_awaiting_file_creation = new std::unordered_map<std::string, std::vector<pid_t> *>;
        thread_awaiting_data =
            new std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "CapioFileManager initialization completed."
                  << std::endl;
    }
    ~CapioFileManager() {
        START_LOG(gettid(), "call()");
        delete thread_awaiting_file_creation;
        delete thread_awaiting_data;
    }

    void setCommitted(const std::filesystem::path &path) const;
    void setCommitted(pid_t tid) const;
    static bool isCommitted(const std::filesystem::path &path);
    void checkAndUnlockThreadAwaitingData(const std::string &path) const;
    static void increaseCloseCount(const std::filesystem::path &path);
    void addThreadAwaitingData(std::string path, int tid, size_t expected_size) const;
    void unlockThreadAwaitingCreation(std::string path) const;
    void addThreadAwaitingCreation(std::string path, pid_t tid) const;
    void deleteFileAwaitingCreation(std::string path) const;
    [[nodiscard]] std::vector<std::string> getFileAwaitingCreation() const;
    [[nodiscard]] std::vector<std::string> getFileAwaitingData() const;
};

CapioFileManager *file_manager;

#include "fs_monitor.hpp"

#include "file_manager_impl.hpp"

#endif // FILE_MANAGER_HEADER_HPP
