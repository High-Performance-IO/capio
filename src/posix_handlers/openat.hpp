//
// Created by marco on 05/09/23.
//

#ifndef CAPIO_OPENAT_HPP
#define CAPIO_OPENAT_HPP


int add_open_request(const char *pathname,size_t fd,int mode,long my_tid) {
    char c_str[256];

    if (mode == 0) {
        sprintf(c_str, "crax %ld %ld %s",my_tid, fd, pathname);
        buf_requests->write(c_str, 256 * sizeof(char));
        off64_t res;
        (*bufs_response)[my_tid]->read(&res);
        return res;
    } else if (mode == 1)
        sprintf(c_str, "crat %ld %ld %s", my_tid, fd, pathname);
    else if (mode == 2)
        sprintf(c_str, "open %ld %ld %s", my_tid, fd, pathname);
    else {
        std::cerr << "error add_open_request mode " << mode << std::endl;
        exit(1);
    }
    buf_requests->write(c_str, 256 * sizeof(char)); //TODO: max upperbound for pathname
    return 0;
}



int capio_openat(int dirfd, const char* pathname, int flags, bool is_creat, long my_tid) {

    CAPIO_DBG("capio_openat %s\n", pathname);

    std::string path_to_check;
    if(is_absolute(pathname)) {
        path_to_check = pathname;

        CAPIO_DBG("capio_openat absolute %s\n", path_to_check.c_str());

    }
    else {
        if(dirfd == AT_FDCWD) {
            path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
            if (path_to_check.length() == 0)
                return -2;

            CAPIO_DBG("capio_openat AT_FDCWD %s\n", path_to_check.c_str());

        }
        else {
            if (is_directory(dirfd) != 1)
                return -2;
            std::string dir_path;
            auto it = capio_files_descriptors->find(dirfd);
            if (it == capio_files_descriptors->end())
                dir_path = get_dir_path(pathname, dirfd);
            else
                dir_path = it->second;
            if (dir_path.length() == 0)
                return -2;
            std::string pathstr = pathname;
            if (pathstr.substr(0, 2) == "./") {
                path_to_check = dir_path + pathstr.substr(1, pathstr.length() - 1);

                CAPIO_DBG("path modified %s\n", pathname);

            }
            else if (std::string(pathname) == ".") {
                path_to_check = dir_path;

            }
            else if (std::string(pathname) == "..") {
                path_to_check = get_capio_parent_dir(dir_path);
            }
            else {
                path_to_check = dir_path + "/" + pathname;
            }
            CAPIO_DBG("capio_openat with dirfd path to check %s dirpath %s\n", path_to_check.c_str(), dir_path.c_str());

        }
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());


    CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());

    if (res.first == capio_dir->end()) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
            err_exit("capio_open, /dev/null opening");
        }
        if (!is_creat)
            is_creat = (flags & O_CREAT) == O_CREAT;
        add_open_request(path_to_check.c_str(), fd, is_creat, my_tid);
        off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(syscall(SYS_gettid)) + "_" + std::to_string(fd), sizeof(off64_t));
        *p_offset = 0;
        off64_t* init_size = new off64_t;
        *init_size = file_initial_size;
        if ((flags & O_DIRECTORY) == O_DIRECTORY)
            flags = flags | O_LARGEFILE;
        if ((flags & O_CLOEXEC) == O_CLOEXEC) {
            CAPIO_DBG("open with O_CLOEXEC\n");
            flags &= ~O_CLOEXEC;
            files->insert({fd, std::make_tuple(p_offset, init_size, flags, FD_CLOEXEC)});
        }
        else
            files->insert({fd, std::make_tuple(p_offset, init_size, flags, 0)});
        (*capio_files_descriptors)[fd] = path_to_check;
        capio_files_paths->insert(path_to_check);
        if ((flags & O_APPEND) == O_APPEND) {
            capio_lseek(fd, 0, SEEK_END);
        }
        CAPIO_DBG("capio_openat returning %d %s\n", fd, pathname);

        return fd;
        //}
    }
    else {

        CAPIO_DBG("capio_openat returning -2 %s\n", pathname);

        return -2;
    }
}

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    int dirfd = static_cast<int>(arg0);
    const char *pathname = reinterpret_cast<const char *>(arg1);
    int flags = static_cast<int>(arg2);
    int res = capio_openat(dirfd, pathname, flags, false, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_OPENAT_HPP
