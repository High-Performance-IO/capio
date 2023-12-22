#ifndef CAPIO_COMMON_LOGGER_HPP
#define CAPIO_COMMON_LOGGER_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <utility>

#include "constants.hpp"
#include "syscall.hpp"
#if defined(CAPIOLOG) && defined(__CAPIO_POSIX)
#include "syscallnames.h"
#endif

#ifndef __CAPIO_POSIX
std::ofstream logfile; // if building for server, self contained logfile
#else
FILE *logfileFP;
bool logfileOpen = false;
#endif

bool loggingSyscall = false; // this variable tells the logger that syscall logging has started and
                             // we are not in setup phase

thread_local int current_log_level = 0;

#ifndef CAPIO_MAX_LOG_LEVEL // capio max log level. defaults to -1, where
// everythong is logged
#define CAPIO_MAX_LOG_LEVEL -1
#endif

int CAPIO_LOG_LEVEL = -1;

inline char *get_capio_log_filename() {

    static char *log_filename = nullptr;

    if (log_filename == nullptr) {
        log_filename = std::getenv("CAPIO_LOGFILE");
        if (log_filename == nullptr) {
            log_filename    = new char;
            log_filename[0] = '\0';
        }
    }
    return log_filename;
}

void log_write_to(char *buffer, size_t bufflen) {
#ifdef __CAPIO_POSIX
    if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
        capio_syscall(SYS_write, fileno(logfileFP), buffer, bufflen);
        capio_syscall(SYS_write, fileno(logfileFP), "\n", 1);
        fflush(logfileFP);
    }
#else
    if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
        logfile << buffer << "\n";
        logfile.flush();
    }

#endif
}

class Logger {
  private:
    long int tid;
    char invoker[256]{0};
    char file[256]{0};
    char format[CAPIO_LOG_MAX_MSG_LEN]{0};

  public:
    inline Logger(const char invoker[], const char file[], int line, long int tid,
                  const char *message, ...) {
#ifndef __CAPIO_POSIX
        if (!logfile.is_open()) {
            // NOTE: should never get to this point as capio_server opens up the log file while
            // parsing command line arguments. This is only for failsafe purposte
            logfile.open(std::string(CAPIO_SERVER_DEFAULT_LOG_FILE_NAME) + std::to_string(tid) +
                             ".log",
                         std::ofstream::out);
        }
#else
        if (!logfileOpen) {
            auto logfile_name = get_capio_log_filename();

            if (logfile_name[0] != '\0') {
                logfileFP   = fopen(logfile_name, "w");
                logfileOpen = true;
            } else {
                logfileFP   = fopen(CAPIO_APP_LOG_FILE_NAME, "w");
                logfileOpen = true;
            }
        }
#endif
        strncpy(this->invoker, invoker, sizeof(this->invoker));
        strncpy(this->file, file, sizeof(this->file));
        this->tid = tid;

        va_list argp, argpc;

        sprintf(format, LOG_PRE_MSG, this->tid, this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

        va_start(argp, message);
        va_copy(argpc, argp);

#if defined(CAPIOLOG) && defined(__CAPIO_POSIX)
        if (current_log_level == 0 && loggingSyscall) {
            int syscallNumber = va_arg(argp, int);
            auto buf1         = reinterpret_cast<char *>(capio_syscall(
                SYS_mmap, nullptr, 50, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            sprintf(buf1, LOG_CAPIO_START_REQUEST, sys_num_to_string(syscallNumber), syscallNumber);
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

    inline void log(const char *message, ...) {
        va_list argp, argpc;

        sprintf(format, LOG_PRE_MSG, this->tid, this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

#ifndef __CAPIO_POSIX
        // if the log message to serve a new request or concludes, add new spaces
        if (strcmp(invoker, "capio_server") == 0 &&
            strcmp(CAPIO_SERVER_LOG_START_REQUEST_MSG, message) == 0) {
            logfile << "\n\n" << message << "\n" << std::flush;
            return;
        }
        if (strcmp(invoker, "capio_server") == 0 &&
            strcmp(CAPIO_SERVER_LOG_END_REQUEST_MSG, message) == 0) {
            logfile << message << "\n\n" << std::flush;
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

    inline ~Logger() {
        current_log_level--;
        sprintf(format, LOG_PRE_MSG, this->tid, this->invoker);
        size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, "returned");

        log_write_to(format, strlen(format));
#ifdef __CAPIO_POSIX
        if (current_log_level == 0 && loggingSyscall) {
            log_write_to(const_cast<char *>(LOG_CAPIO_END_REQUEST), strlen(LOG_CAPIO_END_REQUEST));
        }
#endif
    }
};

#ifdef CAPIOLOG
#define ERR_EXIT(message, ...) (log.log(message, ##__VA_ARGS__), exit(EXIT_FAILURE))
#define LOG(message, ...) log.log(message, ##__VA_ARGS__)
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#define START_SYSCALL_LOGGING() loggingSyscall = true;
#else
#define ERR_EXIT(message, ...) exit(EXIT_FAILURE)
#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define START_SYSCALL_LOGGING()
#endif

#endif // CAPIO_COMMON_LOGGER_HPP
