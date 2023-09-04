#ifndef CAPIO_FILESYS_UTILS_HPP
#define CAPIO_FILESYS_UTILS_HPP


static inline blkcnt_t get_nblocks(off64_t file_size) {
    if (file_size % 4096 == 0)
        return file_size / 512;

    return file_size / 512 + 8;
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


std::string get_capio_parent_dir(std::string path) {
    auto pos = path.rfind('/');
    return path.substr(0, pos);
}


//TODO: doesn't work for general paths like ../dir/../dir/../dir/file.txt
std::string create_absolute_path(const char *pathname,
                                 std::string *capio_dir,
                                 std::string *current_dir,
                                 CPStatEnabled_t *stat_enabled) {
    char *abs_path = (char *) malloc(sizeof(char) * PATH_MAX);
    if (abs_path == NULL)
        err_exit("abs_path create_absolute_path", "create_absolute_path");
    if (*current_dir != *capio_dir) {

        CAPIO_DBG("current dir changed by capiodir\n");

        std::string path(pathname);
        std::string res_path = "";
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
    long int my_tid = syscall_no_intercept(SYS_gettid);
#else
    long int my_tid = syscall(SYS_gettid);
#endif
    (*stat_enabled)[my_tid] = false;
    char *res_realpath = realpath(pathname, abs_path);
    (*stat_enabled)[my_tid] = true;
    if (res_realpath == NULL) {
        int i = strlen(pathname);
        bool found = false;
        bool no_slash = true;
        char *pathname_copy = (char *) malloc(sizeof(char) * (strlen(pathname) + 1));
        strcpy(pathname_copy, pathname);
        while (i >= 0 && !found) {
            if (pathname[i] == '/') {
                no_slash = false;
                pathname_copy[i] = '\0';
                abs_path = realpath(pathname_copy, NULL);
                if (abs_path != NULL)
                    found = true;
            }
            --i;
        }
        if (no_slash) {
            if(getcwd(abs_path, PATH_MAX) == NULL){
                return NULL;
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


void write_to_disk(const int fd, const int offset, const void *buffer, const size_t count,
                   CPFileDescriptors_t *capio_files_descriptors) {
    auto it = capio_files_descriptors->find(fd);
    if (it == capio_files_descriptors->end()) {
        std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
    }
    std::string path = it->second;
    int filesystem_fd = open(path.c_str(),
                             O_WRONLY);//TODO: maybe not efficient open in each write and why O_APPEND (without lseek) does not work?
    if (filesystem_fd == -1) {
        std::cerr << "capio client error: impossible write to disk capio file " << fd << std::endl;
        exit(1);
    }
    if (lseek(filesystem_fd, offset, SEEK_SET) == -1)
        err_exit("lseek write_to_disk", "write_to_disk");
    ssize_t res = write(filesystem_fd, buffer, count);
    if (res == -1) {
        err_exit("capio error writing to disk capio file ", "write_to_disk");
    }
    if ((size_t) res != count) {
        std::cerr << "capio error write to disk: only " << res << " bytes written of " << count << std::endl;
        exit(1);
    }
    if (close(filesystem_fd) == -1) {
        std::cerr << "capio impossible close file " << filesystem_fd << std::endl;
        exit(1);
    }
    //SEEK_HOLE SEEK_DATA
}

void read_from_disk(int fd, int offset, void *buffer, size_t count,
                    CPFileDescriptors_t *capio_files_descriptors) {
    auto it = capio_files_descriptors->find(fd);
    if (it == capio_files_descriptors->end()) {
        std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
    }
    std::string path = it->second;
    int filesystem_fd = open(path.c_str(), O_RDONLY);//TODO: maybe not efficient open in each read
    if (filesystem_fd == -1) {
        err_exit("capio client error: impossible to open file for read from disk", "read_from_disk");
    }
    off_t res_lseek = lseek(filesystem_fd, offset, SEEK_SET);
    if (res_lseek == -1) {
        err_exit("capio client error: lseek in read from disk", "read_from_disk");
    }
    ssize_t res_read = read(filesystem_fd, buffer, count);
    if (res_read == -1) {
        err_exit("capio client error: read in read from disk", "read_from_disk");
    }
    if (close(filesystem_fd) == -1) {
        err_exit("capio client error: close in read from disk", "read_from_disk");
    }
}

int rename_capio_files(std::string oldpath_abs,std::string newpath_abs,
                       CPFilesPaths_t *capio_files_paths ,CPBufRequest_t *buf_requests ,
                       CPBufResponse_t *bufs_response ) {
#ifdef _COMPILE_CAPIO_POSIX
    long int tid = syscall_no_intercept(SYS_gettid);
#else
    long int tid = syscall(SYS_gettid);
#endif
    capio_files_paths->erase(oldpath_abs);
    char msg[256];
    sprintf(msg, "rnam %s %s %ld", oldpath_abs.c_str(), newpath_abs.c_str(), tid);
    buf_requests->write(msg, 256 * sizeof(char));
    off64_t res;
    (*bufs_response)[tid]->read(&res);
    return res;
}

#endif //CAPIO_FILESYS_UTILS_HPP
