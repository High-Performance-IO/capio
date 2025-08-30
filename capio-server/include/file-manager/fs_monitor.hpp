#ifndef CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#define CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#include <capio/logger.hpp>
#include <thread>
#include <utils/configuration.hpp>
/**
 * @brief Class that monitors the composition of the CAPIO_DIR directory.
 *
 */
class FileSystemMonitor {
    std::thread *th;

    bool *continue_execution = new bool;

    static void print_message_error(const std::string &func, const std::exception &exception);

  public:
    /**
     * @brief Main thread execution loop. Main idea is to check
     * whether the files exists on the file system. Then if they exists, wake both thread waiting
     * for file existence and files waiting for data, as the check on the file size (ie. if there is
     * enough data) is carried out by the CapioFileManager class and not by the file_system monitor
     * component itself. At creation a thread is spawned that will continue until a process signals
     * it to stop by setting the continue_execution parameter to false.
     *
     * @param continue_execution
     */
    static void _main(const bool *continue_execution);

    explicit FileSystemMonitor();

    ~FileSystemMonitor();
};

inline FileSystemMonitor *fs_monitor;

#endif // CAPIO_FS_FILE_SYSTEM_MONITOR_HPP