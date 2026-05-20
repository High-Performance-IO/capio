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

inline bool continue_on_error = false;

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

#ifdef __CAPIO_POSIX
// POSIX interceptor: logging starts disabled and is enabled explicitly
// after setup to prevent re-entrancy into the interceptor itself.
inline thread_local bool enable_logger = false;
#else
// Server / non-POSIX: logging is always active from the first call.
inline thread_local bool enable_logger = true;
#endif

#ifndef CAPIO_MAX_LOG_LEVEL
#define CAPIO_MAX_LOG_LEVEL -1
#endif
inline int CAPIO_LOG_LEVEL = CAPIO_MAX_LOG_LEVEL;

inline long long current_time_in_millis() {
    timespec ts{};
    static long long start_time = -1;
    if (start_time == -1) {
        capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
        start_time = static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
    }
    capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000 - start_time;
}

class SyscallLoggingSuspender {
  public:
    SyscallLoggingSuspender() { enable_logger = false; }
    ~SyscallLoggingSuspender() { enable_logger = true; }
};

/**
 * @brief Logging front-end, parameterised on an Adapter for the I/O backend.
 *
 * Adapter contract
 * ----------------
 *   // Called once on entry (current_log_level == 1 after increment).
 *   // Receives all metadata so the adapter can open a structured record.
 *   static void writeOpening(unsigned long ts, const char *invoker,
 *                             const char *file, int line,
 *                             const char *message_format, va_list args);
 *
 *   // Called for every LOG() inside a scope.
 *   static void printFormatted(unsigned long ts, const char *invoker,
 *                               const char *file, int line,
 *                               const char *output_template,
 *                               const char *message_format, va_list args);
 *
 *   // Called on scope exit to close the structured record.
 *   static void writeEpilogue();
 *
 *   // Legacy write — kept for ERR_EXIT paths; adapters may ignore it.
 *   static void write(const char *buf, size_t len);
 */
template <typename Adapter> class TemplateLogger {
    static thread_local int current_log_level;

    char invoker[256]{0};
    char file[256]{0};
    unsigned int line{0};
    long int tid{0};

    Adapter adapter;

  public:
    TemplateLogger(const char invoker[], const char file[], unsigned int line, long int tid,
                   const char *message, ...) {
        this->tid  = tid;
        this->line = line;
        strncpy(this->invoker, invoker, sizeof(this->invoker) - 1);
        strncpy(this->file, file, sizeof(this->file) - 1);

        // Only track nesting when logging is actually active on this thread.
        // If we incremented unconditionally, a START_LOG during the setup
        // phase (before ENABLE_LOGGER) would leave current_log_level at 1
        // for the rest of the thread's lifetime, making every subsequent
        // top-level call look like a nested one.
        if (!enable_logger) {
            return;
        }

        current_log_level++;

        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            va_list argp;
            va_start(argp, message);

            if (current_log_level == 1) {
                adapter.writeOpening(current_time_in_millis(), this->invoker, this->file,
                                     this->line, message, argp);
            } else {
                adapter.printFormatted(current_time_in_millis(), this->invoker, this->file,
                                       this->line, CAPIO_LOG_PRE_MSG, message, argp);
            }

            va_end(argp);
        }
    }

    TemplateLogger(const TemplateLogger &)            = delete;
    TemplateLogger &operator=(const TemplateLogger &) = delete;

    ~TemplateLogger() {
        if (!enable_logger) {
            return; // level was never incremented for this instance
        }

        if (current_log_level == 1 &&
            (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0)) {
            // Capture the exit timestamp here, as close to the actual return
            // as possible, and pass it into the adapter so it doesn't need
            // to call current_time_in_millis() itself.
            adapter.writeEpilogue(static_cast<unsigned long>(current_time_in_millis()));
        }
        current_log_level--;
    }

    void log(const char *message, ...) {
        if (!enable_logger) {
            return;
        }

        // current_log_level is only incremented when enable_logger is true,
        // so this check is always consistent with the constructor.
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            va_list argp;
            va_start(argp, message);
            adapter.printFormatted(current_time_in_millis(), this->invoker, this->file, this->line,
                                   CAPIO_LOG_PRE_MSG, message, argp);
            va_end(argp);
        }
    }

    std::string getLogFileName() { return adapter.getLogFileName(); }

    /**
     * Resets the per-thread nesting counter to zero.
     * Called by the server-side START_LOG macro before constructing a new
     * Logger, so that every top-level server call always opens a fresh
     * JSON object even when invoked from inside an existing Logger scope
     * (e.g. the server main loop calling helper functions that also use
     * START_LOG).  Not used in the POSIX build where nesting is meaningful.
     */
    static void reset_log_level() { current_log_level = 0; }
};

template <typename T> inline thread_local int TemplateLogger<T>::current_log_level = 0;

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

// On the POSIX build, enable_logger starts false and is flipped explicitly;
// nesting is meaningful so current_log_level is never reset.
// On the server build, enable_logger starts true (see declaration above) and
// current_log_level is reset to 0 on every START_LOG so that each top-level
// request handler always opens its own JSON object, even when called from
// inside an outer Logger scope.
#ifdef __CAPIO_POSIX
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#else
#define START_LOG(tid, message, ...)                                                               \
    Logger::reset_log_level();                                                                     \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#endif

#define ENABLE_LOGGER() enable_logger = true
#define DISABLE_LOGGER()                                                                           \
    SyscallLoggingSuspender sls {}

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
#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        __SHM_CHECK_CLI_MSG;                                                                       \
    }
#define DBG(tid, lambda)
#define ENABLE_LOGGER()
#define DISABLE_LOGGER()

#endif

#endif // CAPIO_COMMON_LOGGER_HPP