#ifndef CAPIO_POSIX_HANDLERS_MKDIR_HPP
#define CAPIO_POSIX_HANDLERS_MKDIR_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline off64_t request_mkdir(std::string path_to_check,long tid){

  CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: enter\n", tid, path_to_check.c_str());

    auto res_mismatch = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());
    if (res_mismatch.first == capio_dir->end()) {
        if (capio_dir->size() == path_to_check.size()) {
            return -2;

        } else {
            if (capio_files_paths->find(path_to_check) != capio_files_paths->end()) {
                errno = EEXIST;
                CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: path not found, return -1\n", tid, path_to_check.c_str());
                return -1;
            }
            CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: add mkdir_request\n", tid, path_to_check.c_str());
            off64_t res = mkdir_request(path_to_check.c_str(), tid);
            if (res == 1) {
                CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: mkdir_request returned 1, return -1\n", tid, path_to_check.c_str());
                return -1;
            } else {
                CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: mkdir_request returned %d, return %d\n", tid, path_to_check.c_str(), res, res);
                capio_files_paths->insert(path_to_check);
                return res;
            }
        }
    } else {
        CAPIO_DBG("request_mkdir TID[%ld], PATH[%s]: external file, return -2\n", tid, path_to_check.c_str());
        return -2;
    }
}

inline off64_t capio_mkdir(const char *pathname,mode_t mode,long tid) {

    CAPIO_DBG("capio_mkdir TID[%ld] PATHNAME[%s] MODE[%d]: enter\n", tid, pathname, mode);

    std::string path_to_check;
    if (is_absolute(pathname)) {
        path_to_check = pathname;
    } else {
        path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
        if (path_to_check.length() == 0) {
            CAPIO_DBG("capio_mkdir TID[%ld] PATHNAME[%s] MODE[%d]: invalid path, return -2\n", tid, pathname, mode);
            return -2;
        }
    }

    CAPIO_DBG("capio_mkdir TID[%ld] PATHNAME[%s] MODE[%d]: delegate to request_mkdir\n", tid, pathname, mode);

    return request_mkdir(path_to_check, tid);
}


int mkdir_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long tid){

    const char *pathname = reinterpret_cast<const char *>(arg0);
    off64_t res = capio_mkdir(pathname, static_cast<mode_t>(arg1), tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif // CAPIO_POSIX_HANDLERS_MKDIR_HPP
