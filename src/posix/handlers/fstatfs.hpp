#ifndef CAPIO_POSIX_HANDLERS_FSTATFS_HPP
#define CAPIO_POSIX_HANDLERS_FSTATFS_HPP

#include <sys/vfs.h>

#include "globals.hpp"

inline int capio_fstatfs(int fd,struct statfs *buf, long tid) {

    CAPIO_DBG("fstatfs TID[%ld] FD[%d] BUF[0x%08x]: enter", tid, fd, buf);

    if (files->find(fd) != files->end()) {
        std::string path = (*capio_files_descriptors)[fd];
        CAPIO_DBG("fstatfs TID[%ld] FD[%d] BUF[0x%08x]: statfs on CAPIO_DIR ", tid, fd, buf);
        return statfs(capio_dir->c_str(), buf);
    } else {
        CAPIO_DBG("fstatfs TID[%ld] FD[%d] BUF[0x%08x]: external file, return -2", tid, fd, buf);
        return -2;
    }
}

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    auto *buf = reinterpret_cast<struct statfs *>(arg1);
    int res = capio_fstatfs(static_cast<int>(arg0), buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FSTATFS_HPP
