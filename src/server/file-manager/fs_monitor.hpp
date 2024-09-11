#ifndef CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#define CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#include <thread>

class FileSystemMonitor {
    std::thread *th;

    bool *continue_execution = new bool;

  public:
    static void _main(const bool *continue_execution) {

        START_LOG(gettid(), "INFO: instance of FileSystemMonitor");

        timespec sleep{};
        sleep.tv_nsec = 300; // sleep 0.3 seconds
        while (*continue_execution) {

            /**
             * Main idea is to check whether the files exists on the file system.
             * Then if they exists, wake both thread waiting for file existence
             * and files waiting for data, as the check on the file size (ie. if
             * there is enough data) is carried out by the CapioFileManager class
             * and not by the file_system monitor component itself
             */

            for (const auto &file : file_manager->get_file_awaiting_creation()) {
                if (std::filesystem::exists(file)) {
                    LOG("File %s exists. Unlocking thread awaiting for creation", file.c_str());
                    file_manager->unlock_thread_awaiting_creation(file);
                    file_manager->delete_file_awaiting_creation(file);
                    LOG("File %s exists. Checking if enough data is available", file.c_str());
                    // actual update, end eventual removal from map is handled by the
                    // CapioFileManager class and not by the FileSystemMonitor class
                    file_manager->check_and_unlock_thread_awaiting_data(file);
                    LOG("Completed handling.\n\n");
                }
            }

            for (const auto &file : file_manager->get_file_awaiting_data()) {
                if (std::filesystem::exists(file)) {
                    LOG("File %s exists. Checking if enough data is available", file.c_str());
                    // actual update, end eventual removal from map is handled by the
                    // CapioFileManager class and not by the FileSystemMonitor class
                    file_manager->check_and_unlock_thread_awaiting_data(file);
                    LOG("Completed handling.\n\n");
                }
            }

            nanosleep(&sleep, nullptr);
        }
    }

    explicit FileSystemMonitor() {
        START_LOG(gettid(), "call()");
        *continue_execution = true;
        th                  = new std::thread(_main, std::ref(continue_execution));
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER
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

FileSystemMonitor *fs_monitor;

#endif // CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
