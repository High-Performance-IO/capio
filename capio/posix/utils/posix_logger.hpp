#ifndef CAPIO_POSIXLOGGER_HPP
#define CAPIO_POSIXLOGGER_HPP

#include "common/logger.hpp"
#include "common/json_base_logger.hpp"

struct PosixLogWriteAdapter : JsonLogBase<PosixLogWriteAdapter> {

    static thread_local int  fileFD;
    static thread_local char filePath[PATH_MAX];

    // The constructor is called every time a Logger is constructed (i.e.
    // on every START_LOG).  It opens the per-thread log file on first call
    // and is a no-op on subsequent calls.
    explicit PosixLogWriteAdapter() { ensureFileOpen(); }

    // ------------------------------------------------------------------ //
    //  I/O primitives required by JsonLogBase
    //
    //  These are static because JsonLogBase calls them as Derived::xxx().
    //  They call ensureFileOpen() so that even if a new thread reaches a
    //  static write path before its first constructor call, the file is
    //  always valid.
    // ------------------------------------------------------------------ //

    static void rawWriteBytes(const char *buf, int len) {
        ensureFileOpen();
        capio_syscall(SYS_write, fileFD, buf, len);
    }

    static void rawWriteStr(const char *buf) {
        ensureFileOpen();
        capio_syscall(SYS_write, fileFD, buf, strlen(buf));
    }

  private:
    // Opens the per-thread log file exactly once per thread.
    // Safe to call from both instance constructor and static write paths.
    static void ensureFileOpen() {
        if (fileFD != -1) { return; }

        sprintf(filePath, "%s/%s%ld.log",
                getHostLogDir(), getLogPrefix(),
                capio_syscall(SYS_gettid));

        capio_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(),      0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getPosixLogDir(), 0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(),  0755);

        fileFD = capio_syscall(SYS_openat, AT_FDCWD, filePath,
                               O_CREAT | O_WRONLY | O_APPEND, 0644);

        if (fileFD == -1) {
            capio_syscall(SYS_write, fileno(stdout),
                          "Err fopen file: ", strlen("Err fopen file: "));
            capio_syscall(SYS_write, fileno(stdout), filePath, strlen(filePath));
            capio_syscall(SYS_write, fileno(stdout), " ",      1);
            capio_syscall(SYS_write, fileno(stdout), strerror(errno), strlen(strerror(errno)));
            capio_syscall(SYS_write, fileno(stdout), "\n",     1);
            exit(EXIT_FAILURE);
        }
    }

    static const char *getHostname() {
        static char hostname[HOST_NAME_MAX]{'\0'};
        if (hostname[0] == '\0') { gethostname(hostname, HOST_NAME_MAX); }
        return hostname;
    }

    static const char *getLogDir() {
        static char *dir = nullptr;
        if (dir == nullptr) {
            dir = std::getenv("CAPIO_LOG_DIR");
            if (dir == nullptr) {
                dir = new char[strlen(CAPIO_DEFAULT_LOG_FOLDER) + 1];
                strcpy(dir, CAPIO_DEFAULT_LOG_FOLDER);
            }
        }
        return dir;
    }

    static const char *getLogPrefix() {
        static char *prefix = nullptr;
        if (prefix == nullptr) {
            prefix = std::getenv("CAPIO_LOG_PREFIX");
            if (prefix == nullptr) {
                prefix = new char[strlen(CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX) + 1];
                strcpy(prefix, CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX);
            }
        }
        return prefix;
    }

    static const char *getPosixLogDir() {
        static char *dir = nullptr;
        if (dir == nullptr) {
            const char *base = getLogDir();
            dir = new char[strlen(base) + 7]{0};
            sprintf(dir, "%s/posix", base);
        }
        return dir;
    }

    static const char *getHostLogDir() {
        static char *dir = nullptr;
        if (dir == nullptr) {
            const char *posixDir = getPosixLogDir();
            dir = new char[strlen(posixDir) + HOST_NAME_MAX]{0};
            sprintf(dir, "%s/%s", posixDir, getHostname());
        }
        return dir;
    }
};

inline thread_local int  PosixLogWriteAdapter::fileFD            = -1;
inline thread_local char PosixLogWriteAdapter::filePath[PATH_MAX] = {'\0'};

using Logger = TemplateLogger<PosixLogWriteAdapter>;

#endif // CAPIO_POSIXLOGGER_HPP