#ifndef CAPIO_POSIX_HANDLERS_GETCWD_HPP
#define CAPIO_POSIX_HANDLERS_GETCWD_HPP

inline std::string *capio_getcwd(std::string *buf, size_t size, long tid) {
    START_LOG(tid, "call(buf=0x%08x, size=%ld)", buf, size);

    const std::string *cwd = get_current_dir();
    if ((cwd->length() + 1) * sizeof(char) > size) {
        errno = ERANGE;
        return nullptr;
    } else {
        std::strcpy(buf->data(), cwd->c_str());
        return buf;
    }
}

int getcwd_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string buf(reinterpret_cast<char *>(arg0));
    auto size = static_cast<size_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    auto rescw = capio_getcwd(&buf, size, tid);
    if (rescw == nullptr) {
        *result = -errno;
    }
    return 0;
}

#endif // CAPIO_POSIX_HANDLERS_GETCWD_HPP
