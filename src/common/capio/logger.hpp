#ifndef CAPIO_COMMON_LOGGER_HPP
#define CAPIO_COMMON_LOGGER_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

#include "constants.hpp"
#include "syscall.hpp"

#ifdef __CAPIO_POSIX
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* recursive mkdir from https://gist.github.com/ChisholmKyle/0cbedcd3e64132243a39 */
int mkdir_p(const char *dir, const mode_t mode) {
    char tmp[PATH_MAX];
    char *p = nullptr;
    struct stat sb;
    size_t len;

    /* copy path */
    len = strnlen(dir, PATH_MAX);
    if (len == 0 || len == PATH_MAX) {
        return -1;
    }
    memcpy(tmp, dir, len);
    tmp[len] = '\0';

    /* remove trailing slash */
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* check if path exists and is a directory */
    if (capio_syscall(SYS_stat, tmp, &sb) == 0) {
        if (S_ISDIR(sb.st_mode)) {
            return 0;
        }
    }

    /* recursive mkdir */
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            /* test path */
            if (capio_syscall(SYS_stat, tmp, &sb) != 0) {
                /* path does not exist - create directory */
                if (capio_syscall(SYS_mkdir, tmp, mode) < 0) {
                    return -1;
                }
            } else if (!S_ISDIR(sb.st_mode)) {
                /* not a directory */
                return -1;
            }
            *p = '/';
        }
    }
    /* test path */
    if (capio_syscall(SYS_stat, tmp, &sb) != 0) {
        /* path does not exist - create directory */
        if (capio_syscall(SYS_mkdir, tmp, mode) < 0) {
            return -1;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        /* not a directory */
        return -1;
    }
    return 0;
}
#endif

#if defined(CAPIOLOG) && defined(__CAPIO_POSIX)
#include "syscallnames.h"
#endif

#ifndef __CAPIO_POSIX
#include <filesystem>
thread_local std::ofstream logfile; // if building for server, self contained logfile
std::string log_master_dir_name = CAPIO_DEFAULT_LOG_FOLDER;
std::string logfile_prefix      = CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX;
#else
FILE *logfileFP;
bool logfileOpen                       = false;
static char *posix_log_master_dir_name = nullptr;
static char *posix_logfile_prefix      = nullptr;
static char *posix_hostname            = nullptr;
static thread_local char *logfile_path = nullptr;
static char *posix_log_dir_path        = nullptr;
#endif

thread_local int current_log_level = 0;
thread_local bool logging_syscall  = false; // this variable tells the logger that syscall logging
                                            // has started and we are not in setup phase

#ifndef CAPIO_MAX_LOG_LEVEL // capio max log level. defaults to -1, where everything is logged
#define CAPIO_MAX_LOG_LEVEL -1
#endif

int CAPIO_LOG_LEVEL = CAPIO_MAX_LOG_LEVEL;

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
inline void setup_posix_log_filenames() {
    if (posix_log_master_dir_name == nullptr) {
        posix_log_master_dir_name = std::getenv("CAPIO_LOGDIR");
        if (posix_log_master_dir_name == nullptr) {
            posix_log_master_dir_name = new char[strlen(CAPIO_DEFAULT_LOG_FOLDER)];
            strcpy(posix_log_master_dir_name, CAPIO_DEFAULT_LOG_FOLDER);
        }
    }
    if (posix_logfile_prefix == nullptr) {
        posix_logfile_prefix = std::getenv("CAPIO_LOGFILE");
        if (posix_logfile_prefix == nullptr) {
            posix_logfile_prefix = new char[strlen(CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX)];
            strcpy(posix_logfile_prefix, CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX);
        }
    }

    if (posix_hostname == nullptr) {
        posix_hostname = new char[HOST_NAME_MAX];
        gethostname(posix_hostname, HOST_NAME_MAX);
    }
    if (posix_log_dir_path == nullptr) {
        // allocate space for a path in the following structure (including 10 digits for thread id
        // max
        //  log_master_dir_name/posix/hostname/logfile_prefix_<tid>.log
        posix_log_dir_path = new char[strlen(posix_log_master_dir_name) + 1 + 5 + 1 +
                                      HOST_NAME_MAX + 1 + strlen(posix_logfile_prefix) + 13];
        sprintf(posix_log_dir_path, "%s/posix/%s/", posix_log_master_dir_name, posix_hostname);
    }

    if (logfile_path == nullptr) {
        // allocate space for a path in the following structure (including 10 digits for thread id
        // max
        //  log_master_dir_name/posix/logfile_prefix_<tid>.log
        logfile_path = new char[strlen(posix_log_master_dir_name) + 1 + 5 + 1 + HOST_NAME_MAX + 1 +
                                strlen(posix_logfile_prefix) + 13];
        sprintf(logfile_path, "%s/posix/%s/%s%ld.log", posix_log_master_dir_name, posix_hostname,
                posix_logfile_prefix, capio_syscall(SYS_gettid));
    }
}
#endif

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

class SyscallLoggingSuspender {
  public:
    SyscallLoggingSuspender() { logging_syscall = false; }
    ~SyscallLoggingSuspender() { logging_syscall = true; }
};

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
            setup_posix_log_filenames();
            if (mkdir_p(posix_log_dir_path, 0777) == -1) {
                capio_syscall(SYS_write, fileno(stdout),
                              "Err mkdir file: ", strlen("Err mkdir file: "));
                capio_syscall(SYS_write, fileno(stdout), posix_log_dir_path,
                              strlen(posix_log_dir_path));
                capio_syscall(SYS_write, fileno(stdout), "\n", 1);
                exit(EXIT_FAILURE);
            }

            logfileFP = fopen(logfile_path, "w");

            if (logfileFP == nullptr) {
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

        sprintf(format, CAPIO_LOG_PRE_MSG, this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

        va_start(argp, message);
        va_copy(argpc, argp);

#if defined(CAPIOLOG) && defined(__CAPIO_POSIX)
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

    inline void log(const char *message, ...) {
        va_list argp, argpc;

        sprintf(format, CAPIO_LOG_PRE_MSG, this->invoker);
        size_t pre_msg_len = strlen(format);

        strcpy(format + pre_msg_len, message);

#ifndef __CAPIO_POSIX
        // if the log message to serve a new request or concludes, add new spaces
        if (strcmp(invoker, "capio_server") == 0 &&
            (strcmp(CAPIO_LOG_SERVER_REQUEST_START, message) == 0 ||
             strcmp(CAPIO_LOG_SERVER_REQUEST_END, message) == 0)) {
            auto buf1 = reinterpret_cast<char *>(capio_syscall(
                SYS_mmap, nullptr, 50, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            sprintf(buf1, message, capio_syscall(SYS_gettid));
            logfile << buf1 << std::endl;
            capio_syscall(SYS_munmap, buf1, 50);
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
        sprintf(format, CAPIO_LOG_PRE_MSG, this->invoker);
        size_t pre_msg_len = strlen(format);
        strcpy(format + pre_msg_len, "returned");

        log_write_to(format, strlen(format));
#ifdef __CAPIO_POSIX
        if (current_log_level == 0 && logging_syscall) {
            auto buf1 = reinterpret_cast<char *>(capio_syscall(
                SYS_mmap, nullptr, 50, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            log_write_to(const_cast<char *>(CAPIO_LOG_POSIX_SYSCALL_END), strlen(buf1));
            capio_syscall(SYS_munmap, buf1, 50);
        }
#endif
    }
};

#ifdef CAPIOLOG
#define ERR_EXIT(message, ...) (log.log(message, ##__VA_ARGS__), exit(EXIT_FAILURE))
#define LOG(message, ...) log.log(message, ##__VA_ARGS__)
#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)
#define START_SYSCALL_LOGGING() logging_syscall = true
#define SUSPEND_SYSCALL_LOGGING() SyscallLoggingSuspender sls{};
#else
#define ERR_EXIT(message, ...) exit(EXIT_FAILURE)
#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define START_SYSCALL_LOGGING()
#define SUSPEND_SYSCALL_LOGGING()
#endif
#endif // CAPIO_COMMON_LOGGER_HPP
