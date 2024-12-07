#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

#include <mutex>

inline std::mutex creation_mutex;
inline std::mutex data_mutex;
/**
 * @brief Class that handle all the information related to the files handled by capio, as well
 * metadata for such files.
 *
 */
class CapioFileManager {
    std::unordered_map<std::string, std::vector<pid_t>> thread_awaiting_file_creation;
    std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t>> thread_awaiting_data;

    static std::string getAndCreateMetadataPath(const std::string &path);

    static void _unlockThreadAwaitingCreation(const std::string &path,
                                              const std::vector<pid_t> &pids);

    static void _unlockThreadAwaitingData(const std::string &path,
                                          std::unordered_map<pid_t, capio_off64_t> &pids_awaiting);

  public:
    CapioFileManager() {
        START_LOG(gettid(), "call()");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioFileManager initialization completed." << std::endl;
    }
    ~CapioFileManager() { START_LOG(gettid(), "call()"); }

    static uintmax_t get_file_size_if_exists(const std::filesystem::path &path);
    static void increaseCloseCount(const std::filesystem::path &path);
    static bool isCommitted(const std::filesystem::path &path);
    static void setCommitted(const std::filesystem::path &path);
    static void setCommitted(pid_t tid);
    void addThreadAwaitingData(const std::string &path, int tid, size_t expected_size);
    void addThreadAwaitingCreation(const std::string &path, pid_t tid);
    void checkFilesAwaitingCreation();
    void checkFileAwaitingData();
};

inline CapioFileManager *file_manager;

#include "fs_monitor.hpp"

#include "file_manager_impl.hpp"

#endif // FILE_MANAGER_HEADER_HPP
