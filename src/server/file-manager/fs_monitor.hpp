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

    static void print_message_error(const std::string &func, const std::exception &exception) {
        START_LOG(gettid(), "call()");
        std::cout << std::endl
                  << "~~~~~~~~~~~~~~[\033[31mFileSystemMonitor: FATAL "
                     "EXCEPTION\033[0m]~~~~~~~~~~~~~~"
                  << std::endl
                  << "|  Exception thrown while handling method: " << func << " : " << std::endl
                  << "|  TID of offending thread: " << gettid() << std::endl
                  << "|  PID of offending thread: " << getpid() << std::endl
                  << "|  PPID of offending thread: " << getppid() << std::endl
                  << "|  " << std::endl
                  << "|  `" << typeid(exception).name() << ": " << exception.what() << std::endl
                  << "|" << std::endl
                  << "~~~~~~~~~~~~~~[\033[31mFileSystemMonitor: FATAL "
                     "EXCEPTION\033[0m]~~~~~~~~~~~~~~"
                  << std::endl
                  << std::endl;

        ERR_EXIT("%s", exception.what());
    }

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
            try {
                file_manager->checkFilesAwaitingCreation();
            } catch (const std::exception &exception) {
                print_message_error("file_manager->checkFilesAwaitingCreation()", exception);
            }

            try {
                file_manager->checkFileAwaitingData();
            } catch (const std::exception &exception) {
                print_message_error("file_manager->checkFileAwaitingData()", exception);
            }

            try {
                file_manager->checkDirectoriesNFiles();
            } catch (const std::exception &exception) {
                print_message_error("file_manager->checkDirectoriesNFiles()", exception);
            }
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
        try {
            th->join();
        } catch (const std::system_error &exception) {
            print_message_error("~FileSystemMonitor()::th->joing()", exception);
        }

        delete th;
        delete continue_execution;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << "fs_monitor cleanup completed" << std::endl;
    }
};

inline FileSystemMonitor *fs_monitor;

#endif // CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
