#ifndef CAPIO_FGETXATTR_HPP
#define CAPIO_FGETXATTR_HPP

ssize_t capio_fgetxattr(int fd, const char *name, void *value, size_t size) {
    auto it = files->find(fd);
    if (it != files->end()) {
        if (strcmp(name, "system.posix_acl_access") == 0) {
            errno = ENODATA;
            return -1;
        } else {
            std::cerr << "fgetxattr with name " << name << " is not yet supporte in CAPIO" << std::endl;
            exit(1);
        }
    } else
        return -2;
}


int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *name = reinterpret_cast<const char *>(arg1);
    void *value = reinterpret_cast<void *>(arg2);
    size_t size = arg3;
    int res = capio_fgetxattr(static_cast<int>(arg0), name, value, size);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif //CAPIO_FGETXATTR_HPP
