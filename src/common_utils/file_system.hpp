#ifndef CAPIO_FILE_SYSTEM_HPP
#define CAPIO_FILE_SYSTEM_HPP

#include <fstream>

static inline bool is_absolute(const char *pathname) {
    return (pathname ? (pathname[0] == '/') : false);
}


static inline int is_directory(int dirfd) {
    struct stat path_stat;
    int tmp = fstat(dirfd, &path_stat);
    if (tmp != 0) {
        std::cerr << "error (at is_directory(int dirfd) ): stat" << " errno " << errno << " strerror(errno): " << strerror(errno) << " retval= " <<tmp << std::endl;
        return -1;
    }
    return S_ISDIR(path_stat.st_mode);  // 1 is a directory
}

static inline int is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        std::cerr << "error (at is_directory(const char *path) ): stat" << " errno " << errno << " strerror(errno): " << strerror(errno) << std::endl;
        return -1;
    }
    return S_ISDIR(statbuf.st_mode);
}

bool in_dir(std::string path, std::string glob) {
    size_t res = path.find("/", glob.length() - 1);
    return res != std::string::npos;
}

std::string get_parent_dir_path(std::string file_path, std::ofstream* logfile) {
    std::size_t i = file_path.rfind('/');
    if (i == std::string::npos) {
        (*logfile) << "invalid file_path in get_parent_dir_path" << std::endl;
    }
    return file_path.substr(0, i);
}

#endif //CAPIO_FILE_SYSTEM_HPP
