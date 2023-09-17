#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_fcntl(int fd, int cmd, int arg, long tid) {

    CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: enter\n", tid, fd, cmd, arg);

    if ( files->find(fd) != files->end()) {
        switch (cmd) {
            case F_GETFD: {
                int res = std::get<3>((*files)[fd]);
                CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: F_GETFD, return %d instead of FD_CLOEXEC\n", tid, fd, cmd, arg, res);
                return res;
            }

            case F_SETFD: {
                std::get<3>((*files)[fd]) = arg;
                CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: F_SETFD, return 0\n", tid, fd, cmd, arg);
                return 0;
            }

            case F_GETFL: {
                int flags = std::get<2>((*files)[fd]);
                CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: F_GETFL, return %d instead of O_RDONLY|O_LARGEFILE|O_DIRECTORY\n", tid, fd, cmd, arg, flags);
                return flags;
            }

            case F_SETFL: {
                std::get<2>((*files)[fd]) = arg;
                CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: F_SETFL, return 0\n", tid, fd, cmd, arg);
                return 0;
            }


            case F_DUPFD_CLOEXEC: {
                int dev_fd = open("/dev/null", O_RDONLY);

                if (dev_fd == -1)
                    err_exit("open /dev/null", "capio_fcntl");

                int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg); //
                close(dev_fd);

                CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: F_DUPFD_CLOEXEC, return %d\n", tid, fd, cmd, arg, res);

                (*files)[res] = (*files)[fd];
                std::get<3>((*files)[res]) = FD_CLOEXEC;
                (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
                dup_request(fd, res, tid);
                return res;
            }

            default:
                std::cerr << "fcntl with cmd " << cmd << " is not yet supported"<< std::endl;
                exit(1);
        }
    } else {
        CAPIO_DBG("capio_fcntl TID[%ld] FD[%d] CMD[%d] ARG[%d]: external file, return -2\n", tid, fd, cmd, arg);
        return -2;
    }
}


int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    int res = capio_fcntl(static_cast<int>(arg0),static_cast<int>(arg1), static_cast<int>(arg2), tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP
