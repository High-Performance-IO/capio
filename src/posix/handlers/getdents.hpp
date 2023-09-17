#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#include "globals.hpp"
#include "utils/shm.hpp"

inline off64_t round(off64_t bytes, bool is_getdents64) {
  off64_t res = 0;
  off64_t ld_size;
  if (is_getdents64)
    ld_size = THEORETICAL_SIZE_DIRENT64;
  else
    ld_size = THEORETICAL_SIZE_DIRENT;
  while (res + ld_size <= bytes)
    res += ld_size;
  return res;
}

inline off64_t add_getdents_request(int fd,off64_t count,std::tuple<off64_t *,off64_t *, int, int> &t,bool is_getdents64,long tid) {
    off64_t offset_upperbound = is_getdents64 ? getdents64_request(fd, count, tid) : getdents_request(fd, count, tid);
    off64_t end_of_read = *std::get<0>(t) + count;
    if (end_of_read > offset_upperbound)
        end_of_read = offset_upperbound;
    return offset_upperbound;
}

//TODO: too similar to capio_read, refactoring needed
inline ssize_t capio_getdents(int fd, void *buffer, size_t count, bool is_getdents64,long tid) {

    CAPIO_DBG("capio_getdents%s TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: enter\n", (is_getdents64? std::to_string(64).c_str() : ""), tid, fd, buffer, count);

    if (files->find(fd) != files->end()) {
        if (count >= SSIZE_MAX) {
            std::cerr << "src does not support read bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset = std::get<0>(*t);

        off64_t end_of_read = add_getdents_request(fd, count_off, *t, is_getdents64, tid);
        off64_t bytes_read = end_of_read - *offset;

        if (bytes_read > count_off)
            bytes_read = count_off;
        bytes_read = round(bytes_read, is_getdents64);
        read_shm((*threads_data_bufs)[tid].second, *offset, buffer, bytes_read);

        *offset = *offset + bytes_read;

        CAPIO_DBG("capio_getdents%s TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: return %ld\n", (is_getdents64? std::to_string(64).c_str() : ""), tid, fd, buffer, count, bytes_read);

        return bytes_read;
    } else {
        return -2;
    }
}

static inline int getdents_handler_impl(long arg0, long arg1, long arg2, long* result, long tid, bool is64bit){
    struct linux_dirent *dirp = reinterpret_cast<struct linux_dirent *>(arg1);
    int res = capio_getdents(static_cast<int>(arg0), dirp, static_cast<unsigned int>(arg2), is64bit, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return  0;
    }
    return 1;
}

int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){
    return getdents_handler_impl(arg0, arg1, arg2, result, tid, false);
}

int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    return getdents_handler_impl(arg0, arg1, arg2, result, tid, true);
}

#endif // CAPIO_POSIX_HANDLERS_GETDENTS_HPP
