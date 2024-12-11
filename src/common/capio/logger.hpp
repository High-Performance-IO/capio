#ifndef CAPIO_COMMON_LOGGER_HPP
#define CAPIO_COMMON_LOGGER_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "constants.hpp"
#include "syscall.hpp"

inline bool continue_on_error = false; // change behaviour of ERR_EXIT to continue if set to true

#if defined(CAPIO_LOG) && defined(__CAPIO_POSIX)
#include "syscallnames.h"
#endif

#ifndef __CAPIO_POSIX
#include <filesystem>
thread_local std::ofstream logfile; // if building for server, self-contained logfile
std::string log_master_dir_name = CAPIO_DEFAULT_LOG_FOLDER;
std::string logfile_prefix      = CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX;
#else
inline thread_local bool logfileOpen = false;
inline thread_local int logfileFD    = -1;
inline thread_local char logfile_path[PATH_MAX]{'\0'};
#endif

inline thread_local int current_log_level = 0;
inline thread_local bool logging_syscall =
    false; // this variable tells the logger that syscall logging
           // has started and we are not in setup phase

#ifndef CAPIO_MAX_LOG_LEVEL // capio max log level. defaults to -1, where everything is logged
#define CAPIO_MAX_LOG_LEVEL -1
#endif

inline int CAPIO_LOG_LEVEL = CAPIO_MAX_LOG_LEVEL;

#ifndef __CAPIO_POSIX
inline auto open_server_logfile() {
    auto hostname = new char[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);

    const std::filesystem::path output_folder =
        std::string{log_master_dir_name + "/server/" + hostname};

    std::filesystem::create_directories(output_folder);

    const std::filesystem::path logfile_name = output_folder.string() + "/" + logfile_prefix +
                                               std::to_string(capio_syscall(SYS_gettid)) + ".log";

    logfile.open(logfile_name, std::ofstream::out);
    delete[] hostname;

    return logfile_name;
}
#else

inline auto get_hostname() {
    static char *hostname_prefix = nullptr;
    if (hostname_prefix == nullptr) {
        hostname_prefix = new char[HOST_NAME_MAX];
        gethostname(hostname_prefix, HOST_NAME_MAX);
    }
    return hostname_prefix;
}

inline auto get_log_dir() {
    static char *posix_log_master_dir_name = nullptr;
    if (posix_log_master_dir_name == nullptr) {
        posix_log_master_dir_name = std::getenv("CAPIO_LOG_DIR");
        if (posix_log_master_dir_name == nullptr) {
            posix_log_master_dir_name = new char[strlen(CAPIO_DEFAULT_LOG_FOLDER)];
            strcpy(posix_log_master_dir_name, CAPIO_DEFAULT_LOG_FOLDER);
        }
    }
    return posix_log_master_dir_name;
}

inline auto get_log_prefix() {
    static char *posix_logfile_prefix = nullptr;
    if (posix_logfile_prefix == nullptr) {
        posix_logfile_prefix = std::getenv("CAPIO_LOG_PREFIX");
        if (posix_logfile_prefix == nullptr) {
            posix_logfile_prefix = new char[strlen(CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX)];
            strcpy(posix_logfile_prefix, CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX);
        }
    }
    return posix_logfile_prefix;
}

inline auto get_posix_log_dir() {
    static char *posix_log_dir_path = nullptr;
    if (posix_log_dir_path == nullptr) {
        // allocate space for a path in the following structure (including 10 digits for thread id
        // max
        //  log_master_dir_name/posix/hostname/logfile_prefix_<tid>.log
        auto len           = strlen(get_log_dir()) + 7;
        posix_log_dir_path = new char[len]{0};
        sprintf(posix_log_dir_path, "%s/posix", get_log_dir());
    }
    return posix_log_dir_path;
}

inline auto get_host_log_dir() {
    static char *host_log_dir_path = nullptr;
    if (host_log_dir_path == nullptr) {
        // allocate space for a path in the following structure (including 10 digits for thread id
        // max
        //  log_master_dir_name/posix/hostname/logfile_prefix_<tid>.log
        auto len          = strlen(get_posix_log_dir()) + HOST_NAME_MAX;
        host_log_dir_path = new char[len]{0};
        sprintf(host_log_dir_path, "%s/%s", get_posix_log_dir(), get_hostname());
    }
    return host_log_dir_path;
}

inline void setup_posix_log_filename() {
    if (logfile_path[0] == '\0') {
        sprintf(logfile_path, "%s/%s%ld.log", get_host_log_dir(), get_log_prefix(),
                capio_syscall(SYS_gettid));
    }
}
#endif

inline long long current_time_in_millis() {
    timespec ts{};
    static long long start_time = -1;
    if (start_time == -1) {
        capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
        start_time = static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
    }
    capio_syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
    auto time_now = static_cast<long long>(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
    return time_now - start_time;
}

inline void log_write_to(char *buffer, size_t bufflen) {
#ifdef __CAPIO_POSIX
    if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
        capio_syscall(SYS_write, logfileFD, buffer, bufflen);
        capio_syscall(SYS_write, logfileFD, "\n", 1);
    }
#else
    if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
        logfile << buffer << std::endl;
        logfile.flush();
    }

#endif
}

/**
 * @brief Class used to suspend the logging capabilities of CAPIO, by setting the logging_syscall
 * flag to false at instantiation, and restarting the logging at destruction
 *
 */
class SyscallLoggingSuspender {
  public:
    SyscallLoggingSuspender() { logging_syscall = false; }
    ~SyscallLoggingSuspender() { logging_syscall = true; }
};

/**
 * @brief Class that provides logging capabilities to CAPIO. It uses the STL it the component is not
 * the intercepting library, otherwise it uses POSIX defined systemcalls.
 *
 */
class Logger {
  private:
    char invoker[256]{0};
    char file[256]{0};
    char format[CAPIO_LOG_MAX_MSG_LEN]{0};

  public:
    inline Logger(const char invoker[], const char file[], int line, long int tid,
                  const char *message, ...) {
#ifndef __CAPIO_POSIX
        if (!logfile.is_open()) {
            // NOTE: should never get to this point as capio_server opens up the log file while
            // parsing command line arguments. This is only for failsafe purpose
            open_server_logfile();
        }
#else
        if (!logfileOpen) {
            setup_posix_log_filename();
            current_log_level = 0; // reset after clone log level, so to not inherit it

            capio_syscall(SYS_mkdir, get_log_dir(), 0755);
            capio_syscall(SYS_mkdir, get_posix_log_dir(), 0755);
            capio_syscall(SYS_mkdir, get_host_log_dir(), 0755);

            logfileFD = capio_syscall(SYS_open, logfile_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);

            if (logfileFD == -1) {
                capio_syscall(SYS_write, fileno(stdout),
                              "Err fopen file: ", strlen("Err fopen file: "));
                capio_syscall(SYS_write, fileno(stdout), logfile_path, strlen(logfile_path));
                capio_syscall(SYS_write, fileno(stdout), " ", 1);
                capio_syscall(SYS_write, fileno(stdout), strerror(errno), strlen(strerror(errno)));
                capio_syscall(SYS_write, fileno(stdout), "\n", 1);
                exit(EXIT_FAILURE);
            } else {
                logfileOpen = true;
            }
        }
#endif
        strncpy(this->invoker, invoker, sizeof(this->invoker));
        strncpy(this->file, file, sizeof(this->file));

        va_list argp, argpc;

        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

        va_start(argp, message);
        va_copy(argpc, argp);

#if defined(CAPIO_LOG) && defined(__CAPIO_POSIX)
        if (current_log_level == 0 && logging_syscall) {
            int syscallNumber = va_arg(argp, int);
            auto buf1         = reinterpret_cast<char *>(capio_syscall(
                SYS_mmap, nullptr, 50, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            sprintf(buf1, CAPIO_LOG_POSIX_SYSCALL_START, sys_num_to_string(syscallNumber),
                    syscallNumber);
            log_write_to(buf1, strlen(buf1));
            capio_syscall(SYS_munmap, buf1, 50);
        }
#endif

        int size = vsnprintf(nullptr, 0U, format, argp);
        auto buf = reinterpret_cast<char *>(capio_syscall(SYS_mmap, nullptr, size + 1,
                                                          PROT_READ | PROT_WRITE,
                                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);
        log_write_to(buf, strlen(buf));

        va_end(argp);
        va_end(argpc);
        capio_syscall(SYS_munmap, buf, size);
        current_log_level++;
    }

    Logger(const Logger &)            = delete;
    Logger &operator=(const Logger &) = delete;

    inline ~Logger() {
        current_log_level--;
        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, "returned");

        log_write_to(format, strlen(format));
#ifdef __CAPIO_POSIX
        if (current_log_level == 0 && logging_syscall) {
            log_write_to(const_cast<char *>(CAPIO_LOG_POSIX_SYSCALL_END),
                         strlen(CAPIO_LOG_POSIX_SYSCALL_END));
        }
#endif
    }

    inline void log(const char *message, ...) {
        va_list argp, argpc;

        sprintf(format, CAPIO_LOG_PRE_MSG, current_time_in_millis(), this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

#ifndef __CAPIO_POSIX
        // if the log message to serve a new request or concludes, add new spaces
        if (strcmp(invoker, "capio_server") == 0 &&
            (strcmp(CAPIO_LOG_SERVER_REQUEST_START, message) == 0 ||
             strcmp(CAPIO_LOG_SERVER_REQUEST_END, message) == 0)) {
            logfile << message << std::endl;
            return;
        }
#endif

        va_start(argp, message);
        va_copy(argpc, argp);
        int size = vsnprintf(nullptr, 0U, format, argp);
        auto buf = reinterpret_cast<char *>(capio_syscall(SYS_mmap, nullptr, size + 1,
                                                          PROT_READ | PROT_WRITE,
                                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);

        log_write_to(buf, strlen(buf));

        va_end(argp);
        va_end(argpc);
        capio_syscall(SYS_munmap, buf, size);
    }
};

#ifdef CAPIO_LOG
#define ERR_EXIT(message, ...)                                                                     \
    log.log(message, ##__VA_ARGS__);                                                               \
    if (!continue_on_error) {                                                                      \
        exit(EXIT_FAILURE);                                                                        \
    }
#define LOG(message, ...) log.log(message, ##__VA_ARGS__)
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#define START_SYSCALL_LOGGING() logging_syscall = true
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

#define ERR_EXIT(message, ...)                                                                     \
    if (!continue_on_error)                                                                        \
    exit(EXIT_FAILURE)
#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define START_SYSCALL_LOGGING()
#define SUSPEND_SYSCALL_LOGGING()
#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        __SHM_CHECK_CLI_MSG;                                                                       \
    }
#define DBG(tid, lambda)

#endif

#endif // CAPIO_COMMON_LOGGER_HPP
