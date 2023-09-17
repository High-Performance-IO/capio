#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

#include <sys/stat.h>

std::string get_parent_dir_path(const std::string& file_path, std::ofstream* logfile) {
  std::size_t i = file_path.rfind('/');
  if (i == std::string::npos) {
    (*logfile) << "invalid file_path in get_parent_dir_path" << std::endl;
  }
  return file_path.substr(0, i);
}

bool in_dir(const std::string& path, const std::string& glob) {
  size_t res = path.find('/', glob.length() - 1);
  return res != std::string::npos;
}

static inline bool is_absolute(const char *pathname) {
  return pathname != nullptr && (pathname[0] == '/');
}

static inline int is_directory(int dirfd) {
  struct stat path_stat{};
  int tmp = fstat(dirfd, &path_stat);
  if (tmp != 0) {
    std::cerr << "error (at is_directory(int dirfd) ): stat" << " errno " << errno << " strerror(errno): " << std::strerror(errno) << " retval= " <<tmp << std::endl;
    return -1;
  }
  return S_ISDIR(path_stat.st_mode);  // 1 is a directory
}

static inline int is_directory(const char *path) {
  struct stat statbuf{};
  if (stat(path, &statbuf) != 0) {
    std::cerr << "error (at is_directory(const char *path) ): stat" << " errno " << errno << " strerror(errno): " << std::strerror(errno) << std::endl;
    return -1;
  }
  return S_ISDIR(statbuf.st_mode);
}

bool is_prefix(std::string path_1, std::string path_2) {
  auto res = std::mismatch(path_1.begin(), path_1.end(), path_2.begin());
  return res.first == path_2.end();
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
