#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <climits>
#include <cstdlib>
#include <string>

#include <syscall.h>
#include <unistd.h>

#include "capio/filesystem.hpp"

#include "logger.hpp"
#include "requests.hpp"
#include "types.hpp"

off64_t capio_unlink_abs(std::string abs_path, long pid) {
  off64_t res;
  auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
  if (it.first == capio_dir->end()) {
    if (capio_dir->size() == abs_path.size()) {
      std::cerr << "ERROR: unlink to the capio_dir " << abs_path << std::endl;
      exit(1);
    }

    res = unlink_request(abs_path.c_str(), pid);
    if (res == -1)
      errno = ENOENT;
  } else {
    res = -2;
  }
  return res;
}

void copy_file(const std::string& path_1, const std::string& path_2) {
  FILE *fp_1 = fopen(path_1.c_str(), "r");
  if (fp_1 == nullptr)
    err_exit("fopen fp_1 in copy_file", "copy_file");
  FILE *fp_2 = fopen(path_2.c_str(), "w");
  if (fp_2 == nullptr)
    err_exit("fopen fp_2 in copy_file", "copy_file");
  char buf[1024];
  int res;
  while ((res = fread(buf, sizeof(char), 1024, fp_1)) == 1024) {
    fwrite(buf, sizeof(char), 1024, fp_2);
  }
  if (res != 0) {
    fwrite(buf, sizeof(char), res, fp_2);
  }
  if (fclose(fp_1) == EOF)
    err_exit("fclose fp_1", "copy_file");

  if (fclose(fp_2) == EOF)
    err_exit("fclose fp_2", "copy_file");
}

std::string get_capio_parent_dir(const std::string& path) {
  auto pos = path.rfind('/');
  return path.substr(0, pos);
}

static std::string get_dir_path(const char *pathname, int dirfd) {
  char proclnk[128];
  char dir_pathname[PATH_MAX];
  sprintf(proclnk, "/proc/self/fd/%d", dirfd);
  ssize_t r = readlink(proclnk, dir_pathname, PATH_MAX);
  if (r < 0) {
    fprintf(stderr, "failed to readlink\n");
    return "";
  }
  dir_pathname[r] = '\0';
  return dir_pathname;
}

static inline blkcnt_t get_nblocks(off64_t file_size) {
  if (file_size % 4096 == 0)
    return file_size / 512;

  return file_size / 512 + 8;
}

//TODO: doesn't work for general paths like ../dir/../dir/../dir/file.txt
std::string create_absolute_path(const char *pathname,
                                 std::string *capio_dir,
                                 std::string *current_dir,
                                 CPStatEnabled_t *stat_enabled) {
  char *abs_path = (char *) malloc(sizeof(char) * PATH_MAX);
  if (abs_path == nullptr)
    err_exit("abs_path create_absolute_path", "create_absolute_path");
  if (*current_dir != *capio_dir) {

    CAPIO_DBG("current dir changed by capiodir\n");

    std::string path(pathname);
    std::string res_path;
    if (path == ".") {
      res_path = *current_dir;
      return res_path;
    } else if (path == ".." || path == "./..") {
      res_path = get_capio_parent_dir(*current_dir);
      return res_path;
    }
    if (path.find('/') == path.npos) {
      return *current_dir + "/" + path;
    }
    if (path.substr(0, 3) == "../") {
      res_path = get_capio_parent_dir(*current_dir);
      return res_path + path.substr(2, path.length() - 2);
    }
    if (path.substr(0, 2) == "./") {
      path = *current_dir + path.substr(1, path.length() - 1);
      pathname = path.c_str();

      CAPIO_DBG("path modified %s\n", pathname);

      if (is_absolute(pathname)) {
        return path;
      }
    }
    if (path[0] != '.' && path[0] != '/')
      return *current_dir + "/" + path;

  }
  std::string path(pathname);
  if (path.length() > 2) {
    if (path.substr(path.length() - 2, path.length()) == "/.") {
      path = path.substr(0, path.length() - 2);
      pathname = path.c_str();
    } else if (path.substr(path.length() - 3, path.length()) == "/..") {
      path = path.substr(0, path.length() - 3);
      pathname = path.c_str();
    }
  }
#ifdef _COMPILE_CAPIO_POSIX
  long int tid = syscall_no_intercept(SYS_gettid);
#else
  long int tid = syscall(SYS_gettid);
#endif
  (*stat_enabled)[tid] = false;
  char *res_realpath = realpath(pathname, abs_path);
  (*stat_enabled)[tid] = true;
  if (res_realpath == nullptr) {
    int i = strlen(pathname);
    bool found = false;
    bool no_slash = true;
    char *pathname_copy = (char *) malloc(sizeof(char) * (strlen(pathname) + 1));
    strcpy(pathname_copy, pathname);
    while (i >= 0 && !found) {
      if (pathname[i] == '/') {
        no_slash = false;
        pathname_copy[i] = '\0';
        abs_path = realpath(pathname_copy, nullptr);
        if (abs_path != nullptr)
          found = true;
      }
      --i;
    }
    if (no_slash) {
      if(getcwd(abs_path, PATH_MAX) == nullptr){
        return nullptr;
      }
      int len = strlen(abs_path);
      abs_path[len] = '/';
      abs_path[len + 1] = '\0';
      strcat(abs_path, pathname);
      std::string res_path(abs_path);
      free(abs_path);
      return res_path;
    }
    if (found) {
      ++i;
      strncpy(pathname_copy, pathname + i, strlen(pathname) - i + 1);
      strcat(abs_path, pathname_copy);
      free(pathname_copy);
    } else {
      free(pathname_copy);
      free(abs_path);
      return "";
    }
  }
  std::string res_path(abs_path);
  free(abs_path);
  return res_path;
}

bool is_capio_path(std::string path_to_check, std::string *capio_dir) {
  bool res = false;
  auto mis_res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

  CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());

  if (mis_res.first == capio_dir->end()) {
    if (capio_dir->size() == path_to_check.size()) {
      return -2;

    } else {
      res = true;
    }

  }
  return res;
}

int is_capio_file(std::string abs_path, std::string *capio_dir) {
  auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
  if (it.first == capio_dir->end())
    return 0;
  else
    return -1;
}

void read_from_disk(int fd, int offset, void *buffer, size_t count,
                    CPFileDescriptors_t *capio_files_descriptors) {
  auto it = capio_files_descriptors->find(fd);
  if (it == capio_files_descriptors->end()) {
    std::cerr << "src error in write to disk: file descriptor does not exist" << std::endl;
  }
  std::string path = it->second;
  int filesystem_fd = open(path.c_str(), O_RDONLY);//TODO: maybe not efficient open in each read
  if (filesystem_fd == -1) {
    err_exit("src client error: impossible to open file for read from disk", "read_from_disk");
  }
  off_t res_lseek = lseek(filesystem_fd, offset, SEEK_SET);
  if (res_lseek == -1) {
    err_exit("src client error: lseek in read from disk", "read_from_disk");
  }
  ssize_t res_read = read(filesystem_fd, buffer, count);
  if (res_read == -1) {
    err_exit("src client error: read in read from disk", "read_from_disk");
  }
  if (close(filesystem_fd) == -1) {
    err_exit("src client error: close in read from disk", "read_from_disk");
  }
}

off64_t rename_capio_files(const std::string& oldpath_abs,const std::string& newpath_abs, CPFilesPaths_t *capio_files_paths) {
#ifdef _COMPILE_CAPIO_POSIX
  long int tid = syscall_no_intercept(SYS_gettid);
#else
  long int tid = syscall(SYS_gettid);
#endif
  capio_files_paths->erase(oldpath_abs);
  return rename_request(oldpath_abs.c_str(), newpath_abs.c_str(), tid);
}

void write_to_disk(const int fd, const int offset, const void *buffer, const size_t count,
                   CPFileDescriptors_t *capio_files_descriptors) {
  auto it = capio_files_descriptors->find(fd);
  if (it == capio_files_descriptors->end()) {
    std::cerr << "src error in write to disk: file descriptor does not exist" << std::endl;
  }
  std::string path = it->second;
  int filesystem_fd = open(path.c_str(),
                           O_WRONLY);//TODO: maybe not efficient open in each write and why O_APPEND (without lseek) does not work?
  if (filesystem_fd == -1) {
    std::cerr << "src client error: impossible write to disk src file " << fd << std::endl;
    exit(1);
  }
  if (lseek(filesystem_fd, offset, SEEK_SET) == -1)
    err_exit("lseek write_to_disk", "write_to_disk");
  ssize_t res = write(filesystem_fd, buffer, count);
  if (res == -1) {
    err_exit("src error writing to disk src file ", "write_to_disk");
  }
  if ((size_t) res != count) {
    std::cerr << "src error write to disk: only " << res << " bytes written of " << count << std::endl;
    exit(1);
  }
  if (close(filesystem_fd) == -1) {
    std::cerr << "src impossible close file " << filesystem_fd << std::endl;
    exit(1);
  }
  //SEEK_HOLE SEEK_DATA
}

#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
