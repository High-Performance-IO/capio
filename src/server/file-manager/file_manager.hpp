#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

#include <mutex>
std::mutex threads_mutex;
std::mutex data_mutex;
/**
 * @brief Class that handle all the information related to the files handled by capio, as well
 * metadata for such files.
 *
 */
class CapioFileManager {
    std::unordered_map<std::string, std::vector<pid_t> *> *thread_awaiting_file_creation;
    std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>
        *thread_awaiting_data;

    static std::string getAndCreateMetadataPath(const std::string &path);

  public:
    CapioFileManager() {
        START_LOG(gettid(), "call()");
        thread_awaiting_file_creation = new std::unordered_map<std::string, std::vector<pid_t> *>;
        thread_awaiting_data =
            new std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioFileManager initialization completed." << std::endl;
    }
    ~CapioFileManager() {
        START_LOG(gettid(), "call()");
        delete thread_awaiting_file_creation;
        delete thread_awaiting_data;
    }

    static uintmax_t get_file_size_if_exists(const std::filesystem::path &path);
    static void increaseCloseCount(const std::filesystem::path &path);
    static bool isCommitted(const std::filesystem::path &path);
    void setCommitted(const std::filesystem::path &path) const;
    void setCommitted(pid_t tid) const;
    void checkAndUnlockThreadAwaitingData(const std::string &path) const;
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
