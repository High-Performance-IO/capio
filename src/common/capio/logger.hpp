#ifndef CAPIO_COMMON_LOGGER_HPP
#define CAPIO_COMMON_LOGGER_HPP

#include <sys/mman.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "constants.hpp"
#include "syscall.hpp"

#ifndef __CAPIO_POSIX // fix for older version of gcc found on galileo100 and
// leonardo
#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#endif

std::ofstream logfile; // if building for server, self contained logfile

#endif

thread_local int current_log_level = 0;

#ifndef CAPIO_MAX_LOG_LEVEL // capio max log level. defaults to -1, where
// everythong is logged
#define CAPIO_MAX_LOG_LEVEL -1
#endif

#define LOG_PRE_MSG "tid[%ld]-at[%s]: "

int CAPIO_LOG_LEVEL = -1;

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
            logfile.open(std::string(CAPIO_SERVER_DEFAULT_LOG_FILE_NAME) + std::to_string(tid) +
                             ".log",
                         std::ofstream::out);
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
        int size = vsnprintf(nullptr, 0U, format, argp);
        auto buf = reinterpret_cast<char *>(capio_syscall(SYS_mmap, nullptr, size + 1,
                                                          PROT_READ | PROT_WRITE,
                                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);
#ifdef __CAPIO_POSIX
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            capio_syscall(SYS_write, fileno(stderr), buf, size);
            capio_syscall(SYS_write, fileno(stderr), "\n", 1);
            fflush(stderr);
        }
#else
        if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
            logfile << buf << "\n";
            logfile.flush();
        }

#endif
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

        va_start(argp, message);
        va_copy(argpc, argp);
        int size = vsnprintf(nullptr, 0U, format, argp);
        auto buf = reinterpret_cast<char *>(capio_syscall(SYS_mmap, nullptr, size + 1,
                                                          PROT_READ | PROT_WRITE,
                                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        vsnprintf(buf, size + 1, format, argpc);
#ifdef __CAPIO_POSIX
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            capio_syscall(SYS_write, fileno(stderr), buf, size);
            capio_syscall(SYS_write, fileno(stderr), "\n", 1);
            fflush(stderr);
        }
#else
        if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
            logfile << buf << "\n";
            logfile.flush();
        }
#endif
        va_end(argp);
        va_end(argpc);
        capio_syscall(SYS_munmap, buf, size);
    }

    inline ~Logger() {
        current_log_level--;
        sprintf(format, LOG_PRE_MSG, this->tid, this->invoker);
        size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, "returned");
#ifdef __CAPIO_POSIX
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            capio_syscall(SYS_write, fileno(stderr), format, strlen(format));
            capio_syscall(SYS_write, fileno(stderr), "\n", 1);
            fflush(stderr);
        }
#else
        if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
            logfile << format << "\n";
            logfile.flush();
        }
#endif
    }
};

#ifdef CAPIOLOG
#define ERR_EXIT(message, ...) (log.log(message, ##__VA_ARGS__), exit(EXIT_FAILURE))
#define LOG(message, ...) log.log(message, ##__VA_ARGS__)
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#else
#define ERR_EXIT(message, ...) exit(EXIT_FAILURE)
#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#endif

#endif // CAPIO_COMMON_LOGGER_HPP