#ifndef CAPIO_POSIX_HANDLERS_GETCWD_HPP
#define CAPIO_POSIX_HANDLERS_GETCWD_HPP

#include "globals.hpp"

inline char *capio_getcwd(char *buf, size_t size, long tid) {

  CAPIO_DBG("capio_getcwd TID[%ld] BUF[%s] SIZE[%ld]: enter\n", tid, buf, size);

    const char *c_current_dir = current_dir->c_str();
    if ((current_dir->length() + 1) * sizeof(char) > size) {
        errno = ERANGE;
        CAPIO_DBG("capio_getcwd TID[%ld] BUF[%s] SIZE[%ld]: current dir path %s is too long, return nullptr\n", tid, buf, size, current_dir->c_str());
        return nullptr;
    } else {
        strcpy(buf, c_current_dir);
        CAPIO_DBG("capio_getcwd TID[%ld] BUF[%s] SIZE[%ld]: return current dir path %s\n", tid, buf, size, current_dir->c_str());
        return buf;
    }
}

int getcwd_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long tid){

    char *buf = reinterpret_cast<char *>(arg0);
    auto size = static_cast<size_t>(arg1);
    char *rescw = capio_getcwd(buf, size, tid);
    if (rescw == nullptr) {
        *result = -errno;
    }
   return 0;
}

#endif // CAPIO_POSIX_HANDLERS_GETCWD_HPP
