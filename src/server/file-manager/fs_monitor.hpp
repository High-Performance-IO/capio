#ifndef CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#define CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#include <thread>

/**
 * @brief Class that monitors the composition of the CAPIO_DIR directory.
 *
 */
class FileSystemMonitor {
    std::thread *th;

    bool *continue_execution = new bool;

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
    static void _main(const bool *continue_execution) {

        START_LOG(gettid(), "INFO: instance of FileSystemMonitor");

        timespec sleep{};
        sleep.tv_nsec = 300; // sleep 0.3 seconds
        while (*continue_execution) {

            file_manager->checkFilesAwaitingCreation();
            file_manager->checkFileAwaitingData();

            nanosleep(&sleep, nullptr);
        }
    }

    explicit FileSystemMonitor() {
        START_LOG(gettid(), "call()");
        *continue_execution = true;
        th                  = new std::thread(_main, std::ref(continue_execution));
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioFileSystemMonitor initialization completed." << std::endl;
    }

    ~FileSystemMonitor() {
        START_LOG(gettid(), "call()");
        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;
    }
};

inline FileSystemMonitor *fs_monitor;

#endif // CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
