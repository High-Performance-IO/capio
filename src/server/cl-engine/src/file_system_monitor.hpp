#ifndef CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
#define CAPIO_FS_FILE_SYSTEM_MONITOR_HPP

#include <thread>

class FileSystemMonitor {
    std::thread *th;

    bool *continue_execution = new bool;

  public:
    static void _main(const bool *continue_execution) {
        timespec sleep{};
        sleep.tv_nsec = 300; // sleep 0.3 seconds
        while (*continue_execution) {

            auto files_awaiting_creation = client_manager->get_file_awaiting_creation();

            for (const auto &file : files_awaiting_creation) {
                if (std::filesystem::exists(file)) {
                    client_manager->unlock_thread_awaiting_creation(file);
                }
            }

            auto files_awaiting_data = client_manager->get_file_awaiting_data();

            for (auto file : files_awaiting_data) {
                if (std::filesystem::exists(file)) {
                    client_manager->unlock_thread_awaiting_data(file);
                }
            }

            nanosleep(&sleep, nullptr);
        }
    }

    explicit FileSystemMonitor() {
        *continue_execution = true;
        th                  = new std::thread(_main, std::ref(continue_execution));
    }

    ~FileSystemMonitor() {
        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;
    }
};

FileSystemMonitor *fs_monitor;

#endif // CAPIO_FS_FILE_SYSTEM_MONITOR_HPP
