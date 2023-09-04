#ifndef CAPIO_FSTATFS_HPP
#define CAPIO_FSTATFS_HPP
int capio_fstatfs(int fd,struct statfs *buf) {
    int res = 0;

    CAPIO_DBG("fstatfs captured %d", fd);
    if (files->find(fd) != files->end()) {

        CAPIO_DBG("fstatfs of CAPIO captured %d", fd);
        std::string path = (*capio_files_descriptors)[fd];
        return statfs(capio_dir->c_str(), buf);
    } else
        res = -2;
    return res;
}

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    struct statfs *buf = reinterpret_cast<struct statfs *>(arg1);
    int res = capio_fstatfs(static_cast<int>(arg0), buf);


    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_FSTATFS_HPP
