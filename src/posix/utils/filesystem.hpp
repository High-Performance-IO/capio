#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

#include <syscall.h>
#include <unistd.h>

#include "capio/env.hpp"
#include "capio/filesystem.hpp"
#include "capio/logger.hpp"

#include "requests.hpp"
#include "types.hpp"

void copy_file(long tid, const std::string &path_1, const std::string &path_2) {
    START_LOG(tid, "call(%s, %s)", path_1.c_str(), path_2.c_str());

    FILE *fp_1 = fopen(path_1.c_str(), "r");
    if (fp_1 == nullptr)
        ERR_EXIT("fopen fp_1 in copy_file");
    FILE *fp_2 = fopen(path_2.c_str(), "w");
    if (fp_2 == nullptr)
        ERR_EXIT("fopen fp_2 in copy_file");
    char buf[1024];
    int res;
    while ((res = fread(buf, sizeof(char), 1024, fp_1)) == 1024) {
        fwrite(buf, sizeof(char), 1024, fp_2);
    }
    if (res != 0) {
        fwrite(buf, sizeof(char), res, fp_2);
    }
    if (fclose(fp_1) == EOF) {
        ERR_EXIT("fclose fp_1");
    }
    if (fclose(fp_2) == EOF) {
        ERR_EXIT("fclose fp_2");
    }
}

std::string get_capio_parent_dir(const std::string &path) {
    auto pos = path.rfind('/');
    return path.substr(0, pos);
}

std::string get_dir_path(int dirfd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(dirfd=%d)", dirfd);

    auto it = capio_files_descriptors->find(dirfd);
    if (it != capio_files_descriptors->end()) {
        LOG("dirfd %d points to path %s", dirfd, it->second.c_str());
        return it->second;
    } else {
        char proclnk[128];
        char dir_pathname[PATH_MAX];
        sprintf(proclnk, "/proc/self/fd/%d", dirfd);
        ssize_t r = readlink(proclnk, dir_pathname, PATH_MAX);
        if (r < 0) {
            fprintf(stderr, "failed to readlink\n");
            return "";
        }
        dir_pathname[r] = '\0';
        LOG("dirfd %d points to path %s", dirfd, dir_pathname);
        return dir_pathname;
    }
}

inline blkcnt_t get_nblocks(off64_t file_size) {
    if (file_size % 4096 == 0)
        return file_size / 512;

    return file_size / 512 + 8;
}


void read_from_disk(int fd, int offset, void *buffer, size_t count,
                    CPFileDescriptors_t *capio_files_descriptors) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, offset=%d, buffer=0x%08x, count=%ld, capio_file_descriptors=%0x%08x)",
              fd, offset, buffer, count, capio_files_descriptors);
    auto it = capio_files_descriptors->find(fd);
    if (it == capio_files_descriptors->end()) {
        std::cerr << "src error in write to disk: file descriptor does not exist" << std::endl;
    }
    std::string path = it->second;
    int filesystem_fd = open(path.c_str(), O_RDONLY);//TODO: maybe not efficient open in each read
    if (filesystem_fd == -1) {
        ERR_EXIT("src client error: impossible to open file for read from disk");
    }
    off_t res_lseek = lseek(filesystem_fd, offset, SEEK_SET);
    if (res_lseek == -1) {
        ERR_EXIT("src client error: lseek in read from disk");
    }
    ssize_t res_read = read(filesystem_fd, buffer, count);
    if (res_read == -1) {
        ERR_EXIT("src client error: read in read from disk");
    }
    if (close(filesystem_fd) == -1) {
        ERR_EXIT("src client error: close in read from disk");
    }
}

off64_t rename_capio_files(long tid, const std::string &oldpath_abs, const std::string &newpath_abs,
                           CPFilesPaths_t *capio_files_paths) {
    capio_files_paths->erase(oldpath_abs);
    return rename_request(oldpath_abs.c_str(), newpath_abs.c_str(), tid);
}

void write_to_disk(const int fd, const int offset, const void *buffer, const size_t count,
                   CPFileDescriptors_t *capio_files_descriptors) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, offset=%d, buffer=0x%08x, count=%ld, capio_file_descriptors=%0x%08x)",
              fd, offset, buffer, count, capio_files_descriptors);

    auto it = capio_files_descriptors->find(fd);
    if (it == capio_files_descriptors->end()) {
        std::cerr << "src error in write to disk: file descriptor does not exist" << std::endl;
    }
    std::string path = it->second;
    int filesystem_fd = open(path.c_str(),
                             O_WRONLY);//TODO: maybe not efficient open in each write and why O_APPEND (without lseek) does not work?
    if (filesystem_fd == -1) {
        ERR_EXIT("src client error: impossible write to disk src file %d", fd);
    }
    if (lseek(filesystem_fd, offset, SEEK_SET) == -1)
        ERR_EXIT("lseek write_to_disk");
    ssize_t res = write(filesystem_fd, buffer, count);
    if (res == -1) {
        ERR_EXIT("src error writing to disk src file ");
    }
    if ((size_t) res != count) {
        ERR_EXIT("src error write to disk: only %ld bytes written of %ld", res, count);
    }
    if (close(filesystem_fd) == -1) {
        ERR_EXIT("src impossible close file %d", filesystem_fd);
    }
}



#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
