#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

class CapioFileManager {
  private:
    std::unordered_map<std::string, std::vector<pid_t> *> *thread_awaiting_file_creation;
    std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>
        *thread_awaiting_data;

  public:
    CapioFileManager() {
        thread_awaiting_file_creation = new std::unordered_map<std::string, std::vector<pid_t> *>;
        thread_awaiting_data =
            new std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>;
    }
    ~CapioFileManager() {
        delete thread_awaiting_file_creation;
        delete thread_awaiting_data;
    }

    void set_committed(const std::filesystem::path &path) const;
    void set_committed(pid_t tid) const;
    static bool is_committed(const std::filesystem::path &path);
    void check_and_unlock_thread_awaiting_data(std::string path) const;
    void add_thread_awaiting_data(std::string path, int tid, size_t expected_size) const;
    void unlock_thread_awaiting_creation(std::string path) const;
    void add_thread_awaiting_creation(std::string path, pid_t tid) const;
    void delete_file_awaiting_creation(std::string path) const;
    [[nodiscard]] std::vector<std::string> get_file_awaiting_creation() const;
    [[nodiscard]] std::vector<std::string> get_file_awaiting_data() const;
};

CapioFileManager *file_manager;

#include "fs_monitor.hpp"

#include "file_manager_impl.hpp"

#endif // FILE_MANAGER_HEADER_HPP
