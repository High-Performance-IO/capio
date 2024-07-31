#ifndef FILE_MANAGER_HEADER_HPP
#define FILE_MANAGER_HEADER_HPP

class CapioFileManager {
  public:
    static void set_committed(const std::filesystem::path &path);
    static void set_committed(pid_t tid);
    static bool is_committed(const std::filesystem::path &path);
};

#endif // FILE_MANAGER_HEADER_HPP
