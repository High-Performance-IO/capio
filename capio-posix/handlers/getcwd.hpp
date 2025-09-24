#ifndef CAPIO_POSIX_HANDLERS_GETCWD_HPP
#define CAPIO_POSIX_HANDLERS_GETCWD_HPP

#if defined(SYS_getcwd)

int getcwd_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto buf  = reinterpret_cast<char *>(arg0);
    auto size = static_cast<size_t>(arg1);

    START_LOG(syscall_no_intercept(SYS_gettid), "call(buf=0x%08x, size=%ld)", buf, size);

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_getcwd
#endif // CAPIO_POSIX_HANDLERS_GETCWD_HPP