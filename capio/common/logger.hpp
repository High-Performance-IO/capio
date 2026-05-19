#ifndef CAPIO_COMMON_LOGGER_HPP
#define CAPIO_COMMON_LOGGER_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>

#include "constants.hpp"
#include "syscall.hpp"

template <typename T> std::string demangled_name(const T &obj) {
    int status;
    const char *mangled = typeid(obj).name();
    std::unique_ptr<char, void (*)(void *)> demangled(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return status == 0 ? demangled.get() : mangled;
}

inline bool continue_on_error = false; // if set to true, CAPIO does not terminate on ERR_EXIT

inline void raise_termination(const bool raise_exception, const std::string &message) {
    if (raise_exception) {
        throw std::runtime_error(message);
    }
    capio_syscall(SYS_write, stderr, message.c_str(), message.size());
    exit(EXIT_FAILURE);
}

#if defined(CAPIO_LOG) && defined(__CAPIO_POSIX)
#include "syscallnames.h"
#endif

// this variable tells the logger that syscall logging
// has started, and we are not in setup phase
// FIXME: Remove the inline specifier by splitting into header and source code
inline thread_local bool enable_logger = false;

#ifndef CAPIO_MAX_LOG_LEVEL // capio max log level. defaults to -1, where everything is logged
#define CAPIO_MAX_LOG_LEVEL -1
#endif
// FIXME: Remove the inline specifier by splitting into header and source code
inline int CAPIO_LOG_LEVEL = CAPIO_MAX_LOG_LEVEL;

inline long long current_time_in_millis() {
    timespec ts{};
    static long long start_time = -1;
    if (start_time == -1) {
        capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
        start_time = static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
    }
    capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
    const auto time_now = static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
    return time_now - start_time;
}

/**
 * @brief Class used to suspend the logging capabilities of CAPIO, by setting the logging_syscall
 * flag to false at instantiation, and restarting the logging at destruction
 */
class SyscallLoggingSuspender {
  public:
    SyscallLoggingSuspender() { enable_logger = false; }
    ~SyscallLoggingSuspender() { enable_logger = true; }
};

/**
 * @brief Class that provides logging capabilities to CAPIO.
 *
 * Parameterised on a LogWriteAdapter so that the I/O strategy (POSIX
 * syscalls vs. STL streams vs. any custom backend) is a compile-time
 * choice with no virtual dispatch overhead.
 *
 * The default adapter (@ref DefaultLogWriteAdapter) mirrors the behaviour
 * of the old monolithic Logger class for both the server and POSIX builds.
 */
template <typename Adapter> class TemplateLogger {
    static thread_local int current_log_level;
    char invoker[256]{0};
    char file[256]{0};
    char format[CAPIO_LOG_MAX_MSG_LEN]{0};
    unsigned int line{0};
    long int tid{0};

    Adapter adapter;

  public:
    TemplateLogger(const char invoker[], const char file[], unsigned int line, long int tid,
                   const char *message, ...) {
        current_log_level++;
        this->tid  = tid;
        this->line = line;
        strncpy(this->invoker, invoker, sizeof(this->invoker));
        strncpy(this->file, file, sizeof(this->file));

        va_list argp, argpc;
        va_start(argp, message);
        va_copy(argpc, argp);

#ifdef __CAPIO_POSIX
        if (!enable_logger) {
            return;
        }
#endif

        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        const size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, message);

        const int size = vsnprintf(nullptr, 0U, format, argp);
        auto buf       = reinterpret_cast<char *>(capio_syscall(SYS_mmap, nullptr, size + 1,
                                                                PROT_READ | PROT_WRITE,
                                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);

        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            if (current_log_level == 1) {
                adapter.writeOpening();
            }
            adapter.write(this->invoker, this->file, this->line, this->tid, buf, strlen(buf));
        }

        va_end(argp);
        va_end(argpc);
        capio_syscall(SYS_munmap, buf, size);
    }

    TemplateLogger(const TemplateLogger &)            = delete;
    TemplateLogger &operator=(const TemplateLogger &) = delete;

    ~TemplateLogger() {
        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        const size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, "returned");

        adapter.writeRaw(format, strlen(format));

        if (current_log_level == 1 && enable_logger &&
            (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0)) {
            adapter.writeEpilogue();
        }
        current_log_level--;
    }

    void log(const char *message, ...) {
#ifdef __CAPIO_POSIX
        if (!enable_logger) {
            return;
        }
#endif
        va_list argp, argpc;

        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        const size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, message);

        va_start(argp, message);
        va_copy(argpc, argp);
        const int size = vsnprintf(nullptr, 0U, format, argp);
        const auto buf = reinterpret_cast<char *>(
            capio_syscall(SYS_mmap, nullptr, size + 1, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);

        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            if (adapter.isSTLSafe()) {
                adapter.writeRaw(format, strlen(format));
            } else {
                adapter.write(this->invoker, this->file, this->line, this->tid, buf, strlen(buf));
            }
        }

        va_end(argp);
        va_end(argpc);
        capio_syscall(SYS_munmap, buf, size);
    }

    std::string getLogFileName() { return adapter.getLogFileName(); }
};

template <typename T> inline thread_local int TemplateLogger<T>::current_log_level = 0;

// ---------------------------------------------------------------------------
// Macros — identical surface to the old ones; Logger is now a template
// ---------------------------------------------------------------------------
#ifdef CAPIO_LOG

#define ERR_EXIT_EXCEPT_CHOICE(raise_exception, message, ...)                                      \
    log.log(message, ##__VA_ARGS__);                                                               \
    if (!continue_on_error) {                                                                      \
        char err_msg[CAPIO_LOG_MAX_MSG_LEN];                                                       \
        snprintf(err_msg, CAPIO_LOG_MAX_MSG_LEN, message, ##__VA_ARGS__);                          \
        raise_termination(raise_exception, err_msg);                                               \
    }
#define ERR_EXIT(message, ...) ERR_EXIT_EXCEPT_CHOICE(true, message, ##__VA_ARGS__)

#define LOG(message, ...) log.log(message, ##__VA_ARGS__)
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#define START_SYSCALL_LOGGING() enable_logger = true
#define SUSPEND_SYSCALL_LOGGING() SyscallLoggingSuspender sls{};

/**
 * This macro is used to inject code into debug mode. It needs a self calling lambda function,
 * that is a lambda in the following form:
 *
 * [](){}()
 *
 * For example, a debug code to print a value might be injected in this way:
 *
 * int value = 10;
 * DBG(tid, [](int i){printf("%d", i);}(value) );
 *
 * This is useful to print or run debug actions with variables defined outside
 * of the scope of the given lambda function Be careful that the code defined inside the DBG
 * macro is not compiled when building in Release.
 *
 * Be even MORE CAREFUL to not use any STL code inside the DBG lambda function as it could be
 * captured by syscall_intercept (ie do not use std::cout unless you are 100% sure of what you are
 * doing)
 */
#define DBG(tid, lambda)                                                                           \
    {                                                                                              \
        START_LOG(tid, "[  DBG  ]~~~~~~~~~~~~ START ~~~~~~~~~~~~~~[  DBG  ]");                     \
        lambda;                                                                                    \
        LOG("[  DBG  ]~~~~~~~~~~~~ END  ~~~~~~~~~~~~~~[  DBG  ]");                                 \
    }

#else

#define ERR_EXIT_EXCEPT_CHOICE(raise_exception, message, ...)                                      \
    if (!continue_on_error) {                                                                      \
        char err_msg[CAPIO_LOG_MAX_MSG_LEN];                                                       \
        snprintf(err_msg, sizeof(err_msg), message, ##__VA_ARGS__);                                \
        raise_termination(raise_exception, err_msg);                                               \
    }
#define ERR_EXIT(message, ...) ERR_EXIT_EXCEPT_CHOICE(true, message, ##__VA_ARGS__)
#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define START_SYSCALL_LOGGING()
#define SUSPEND_SYSCALL_LOGGING()
#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        __SHM_CHECK_CLI_MSG;                                                                       \
    }
#define DBG(tid, lambda)
#define START_SYSCALL_LOGGING()
#define SUSPEND_SYSCALL_LOGGING()

#endif

#endif // CAPIO_COMMON_LOGGER_HPP