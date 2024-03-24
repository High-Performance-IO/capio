#ifndef CAPIO_POSIX_HANDLERS_GETCWD_HPP
#define CAPIO_POSIX_HANDLERS_GETCWD_HPP

int getcwd_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto buf  = reinterpret_cast<char *>(arg0);
    auto size = static_cast<size_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(buf=0x%08x, size=%ld)", buf, size);

    const std::filesystem::path &cwd = get_current_dir();

    /*
     * Returning control to the kernel when current directory is
     * not a CAPIO path helps to solve timing issues with network
     * file systems such as NFS.
     */
    if (!is_capio_path(cwd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    const size_t length = cwd.native().length();
    if ((length + 1) * sizeof(char) > size) {
        *result = -ERANGE;
    } else {
        LOG("CWD: %s", cwd.c_str());
        cwd.native().copy(buf, size);
        buf[length] = '\0';
        *result     = static_cast<int>(length) + 1;
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // CAPIO_POSIX_HANDLERS_GETCWD_HPP