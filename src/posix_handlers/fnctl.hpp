#ifndef CAPIO_FNCTL_HPP
#define CAPIO_FNCTL_HPP

int capio_fcntl(int fd, int cmd, int arg, long tid) {

    if ( files->find(fd) != files->end()) {

        CAPIO_DBG("capio_fcntl\n");
        switch (cmd) {
            case F_GETFD: {
                int res = std::get<3>((*files)[fd]);
                CAPIO_DBG("capio_fcntl F_GETFD returing %d instead of %d\n", res, FD_CLOEXEC);
                return res;
            }

            case F_SETFD: {
                std::get<3>((*files)[fd]) = arg;
                return 0;
            }

            case F_GETFL: {
                int flags = std::get<2>((*files)[fd]);
                CAPIO_DBG("fcntl F_GETFL returing %d instead of %d\n", flags, O_RDONLY|O_LARGEFILE|O_DIRECTORY);
                return flags;
            }

            case F_SETFL: {
                std::get<2>((*files)[fd]) = arg;
                return 0;
            }


            case F_DUPFD_CLOEXEC: {
                int dev_fd = open("/dev/null", O_RDONLY);

                if (dev_fd == -1)
                    err_exit("open /dev/null", "capio_fcntl");

                int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg); //
                close(dev_fd);
                CAPIO_DBG("capio_fcntl cloexec returning fd %d res %d\n", fd, res);
                (*files)[res] = (*files)[fd];
                std::get<3>((*files)[res]) = FD_CLOEXEC;
                (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
                add_dup_request(fd, res, tid);
                return res;
            }

            default:
                std::cerr << "fcntl with cmd " << cmd << " is not yet supported"<< std::endl;
                exit(1);
        }
    } else
        return -2;
}


int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    int res = capio_fcntl(static_cast<int>(arg0),static_cast<int>(arg1), static_cast<int>(arg2), my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_FNCTL_HPP
