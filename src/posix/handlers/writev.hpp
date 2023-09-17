#ifndef CAPIO_POSIX_HANDLERS_WRITEV_HPP
#define CAPIO_POSIX_HANDLERS_WRITEV_HPP

#include "globals.hpp"
#include "write.hpp"

inline ssize_t capio_writev(int fd,const struct iovec *iov,int iovcnt,long tid) {

    CAPIO_DBG("capio_writev TID[%d] FD[%d] IOV_BASE[%d], IOV_LEN[%d] IOVCNT[%d]: enter\n", tid, fd, iov->iov_base, iov->iov_len, iovcnt);

    auto it = files->find(fd);
    if (it != files->end()) {
      ssize_t tot_bytes = 0;
      ssize_t res = 0;
      int i = 0;
      while (i < iovcnt && res >= 0) {
        size_t iov_len = iov[i].iov_len;
        if (iov_len != 0) {
          res = capio_write(fd, iov[i].iov_base, iov[i].iov_len, tid);
          tot_bytes += res;
        }
        ++i;
      }
      if (res == -1) {

        CAPIO_DBG("capio_writev TID[%d] FD[%d] IOV_BASE[0x%08x], IOV_LEN[%d] IOVCNT[%d]: capio_write failed, return -1\n",
                  tid, fd, iov->iov_base, iov->iov_len, iovcnt);

        return -1;
      } else {
          CAPIO_DBG("capio_writev TID[%d] FD[%d] IOV_BASE[0x%08x], IOV_LEN[%d] IOVCNT[%d]: return %d\n",
                    tid, fd, iov->iov_base, iov->iov_len, iovcnt, tot_bytes);

          return tot_bytes;
      }
    } else {

        CAPIO_DBG("capio_writev TID[%d] FD[%d] IOV_BASE[0x%08x], IOV_LEN[%d] IOVCNT[%d]: external file, return -2\n", tid, fd, iov->iov_base, iov->iov_len, iovcnt);

        return -2;
    }
}

int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    int fd = static_cast<int>(arg0);
    const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
    int iovcnt = static_cast<int>(arg2);

    ssize_t res = capio_writev(fd, iov, iovcnt, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);

        return 0;
    }
    return 1;
}
#endif // CAPIO_POSIX_HANDLERS_WRITEV_HPP
