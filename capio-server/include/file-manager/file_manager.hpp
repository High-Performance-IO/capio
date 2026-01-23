#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

#include <include/utils/types.hpp>
#include <mutex>
#include <string>
#include <unordered_map>

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

    /**
     * @brief Creates the directory structure for the metadata file and proceed to return the path
     * pointing to the metadata token file. For improvements in performances, a hash map is included
     * to cache the computed paths. For thread safety concerns, see
     * https://en.cppreference.com/w/cpp/container#Thread_safety
     *
     * @param path real path of the file
     * @return std::string with the translated capio token metadata path
     */
    static std::string getAndCreateMetadataPath(const std::string &path);

    /**
     * @brief Awakes all threads waiting for the creation of a file
     *
     * @param path file that has just been created
     * @param pids
     */
    static void _unlockThreadAwaitingCreation(const std::string &path,
                                              const std::vector<pid_t> &pids);

    /**
     * @brief Loop between all thread registered on the file path, and check for each
     * one if enough data has been produced. If so, unlock and remove the thread
     *
     * @param path
     * @param pids_awaiting
     */
    static void _unlockThreadAwaitingData(const std::string &path,
                                          std::unordered_map<pid_t, capio_off64_t> &pids_awaiting);

  public:
    CapioFileManager() {
        START_LOG(gettid(), "call()");
        server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioFileManager initialization completed.");
    }

    ~CapioFileManager() {
        START_LOG(gettid(), "call()");
        server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioFileManager cleanup completed.");
    }

    /**
     * @brief Get the file size
     *
     * @param path
     * @return uintmax_t file size if file exists, 0 otherwise
     */
    static uintmax_t get_file_size_if_exists(const std::filesystem::path &path);
    static std::string getMetadataPath(const std::string &path);

    /**
     * @brief Update the CAPIO metadata n_close option by adding one to the current value
     *
     * @param path
     */
    static void increaseCloseCount(const std::filesystem::path &path);

    /**
     * @brief Returns whether the file is committed or not
     *
     * @param path
     * @return true if is committed
     * @return false if it is not
     */
    static bool isCommitted(const std::filesystem::path &path);

    /**
     * @brief Set a CAPIO handled file to be committed
     *
     * @param path
     */
    static void setCommitted(const std::filesystem::path &path);

    /**
     * @brief Set all the files that are currently open, or have been open by a given process to be
     * committed
     *
     * @param tid
     */
    static void setCommitted(pid_t tid);

    /**
     * @brief Register a process waiting on a file to exist and with a file size of at least the
     * expected_size parameter.
     *
     * @param path
     * @param tid
     * @param expected_size
     */
    void addThreadAwaitingData(const std::string &path, int tid, size_t expected_size);

    /**
     * @brief Register a thread to the threads waiting for a file to exists (inside the
     * CapioFSMonitor) for a given file path to exists
     *
     * @param path
     * @param tid
     */
    void addThreadAwaitingCreation(const std::string &path, pid_t tid);

    /**
     * @brief Check for threads awaiting file creation and unlock threads waiting on them
     *
     */
    void checkFilesAwaitingCreation();

    /**
     * @brief check if there are threads waiting for data, and for each one of them check if the
     * file has enough data
     *
     */
    void checkFileAwaitingData();

    /**
     * @brief commit directories that have NFILES inside them if their commit rule is n_files
     */
    void checkDirectoriesNFiles() const;
};

#endif // FILE_MANAGER_HEADER_HPP