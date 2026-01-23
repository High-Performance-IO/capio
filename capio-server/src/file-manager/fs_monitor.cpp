#include <capio/logger.hpp>
#include <include/file-manager/file_manager.hpp>
#include <include/file-manager/fs_monitor.hpp>
#include <include/utils/configuration.hpp>

extern CapioFileManager *file_manager;
extern FileSystemMonitor *fs_monitor;

FileSystemMonitor::FileSystemMonitor() {
    START_LOG(gettid(), "call()");
    *continue_execution = true;
    th                  = new std::thread(_main, std::ref(continue_execution));
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioFileSystemMonitor initialization completed.");
}

void FileSystemMonitor::print_message_error(const std::string &func,
                                            const std::exception &exception) {
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
}

void FileSystemMonitor::_main(const bool *continue_execution) {
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

FileSystemMonitor::~FileSystemMonitor() {
    START_LOG(gettid(), "call()");
    *continue_execution = false;
    try {
        th->join();
    } catch (const std::system_error &exception) {
        print_message_error("~FileSystemMonitor()::th->joing()", exception);
    }

    delete th;
    delete continue_execution;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "CapioFileSystemMonitor cleanup completed.");
}
