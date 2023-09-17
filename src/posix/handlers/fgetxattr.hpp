#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

#include "globals.hpp"

inline int capio_fgetxattr(int fd, const char *name, void *value, size_t size, long tid) {

    CAPIO_DBG("capio_fgetxattr TID[%ld] FD[%d] NAME[%s] VALUE[0x%08x] SIZE[%ld]: enter\n", tid, fd, name, value, size);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (strcmp(name, "system.posix_acl_access") == 0) {
            errno = ENODATA;
            CAPIO_DBG("capio_fgetxattr TID[%ld] FD[%d] NAME[%s] VALUE[0x%08x] SIZE[%ld]: return -1\n", tid, fd, name, value, size);
            return -1;
        } else {
            std::cerr << "fgetxattr with name " << name << " is not yet supporte in CAPIO" << std::endl;
            exit(1);
        }
    } else {
        CAPIO_DBG("capio_fgetxattr TID[%ld] FD[%d] NAME[%s] VALUE[0x%08x] SIZE[%ld]: external file, return -2\n", tid, fd, name, value, size);
        return -2;
    }
}


int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    const char *name = reinterpret_cast<const char *>(arg1);
    void *value = reinterpret_cast<void *>(arg2);
    size_t size = arg3;
    int res = capio_fgetxattr(static_cast<int>(arg0), name, value, size, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
