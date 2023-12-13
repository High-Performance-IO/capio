#ifndef CAPIO_POSIX_HANDLERS_GETCWD_HPP
#define CAPIO_POSIX_HANDLERS_GETCWD_HPP
int getcwd_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto buf  = reinterpret_cast<char *>(arg0);
    auto size = static_cast<size_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(buf=0x%08x, size=%ld)", buf, size);

    const std::string cwd(*get_current_dir());

    if ((cwd.length() + 1) * sizeof(char) > size) {
        *result = -ERANGE;
    } else {
        LOG("CWD: %s", cwd.c_str());
        cwd.copy(buf, size);
        buf[cwd.length()] = '\0';
    }
    return POSIX_SYSCALL_HANDLED_BY_CAPIO;
}

#endif // CAPIO_POSIX_HANDLERS_GETCWD_HPP