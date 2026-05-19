#ifndef CAPIO_POSIXLOGGER_HPP
#define CAPIO_POSIXLOGGER_HPP

#include "common/logger.hpp"
inline thread_local bool fileOpen = false;
inline thread_local int fileFD    = -1;
inline thread_local char filePath[PATH_MAX];

struct PosixLogWriteAdapter {
  private:
    static const char *getHostname() {
        static char hostname[HOST_NAME_MAX]{'\0'};
        if (hostname[0] == '\0') {
            gethostname(hostname, HOST_NAME_MAX);
        }
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
            dir              = new char[strlen(base) + 7]{0}; // "/posix\0"
            sprintf(dir, "%s/posix", base);
        }
        return dir;
    }

    static const char *getHostLogDir() {
        static char *dir = nullptr;
        if (dir == nullptr) {
            const char *posixDir = getPosixLogDir();
            dir                  = new char[strlen(posixDir) + HOST_NAME_MAX]{0};
            sprintf(dir, "%s/%s", posixDir, getHostname());
        }
        return dir;
    }

    static void writeToFD(const char *buf, const size_t len) {
        if (!fileOpen) {
            return;
        }
        capio_syscall(SYS_write, fileFD, buf, len);
        capio_syscall(SYS_write, fileFD, "\n", 1);
    }

  public:
    explicit PosixLogWriteAdapter() {

        if (fileOpen) {
            return;
        }

        sprintf(filePath, "%s/%s%ld.log", getHostLogDir(), getLogPrefix(),
                capio_syscall(SYS_gettid));

        capio_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(), 0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getPosixLogDir(), 0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(), 0755);

        fileFD = capio_syscall(SYS_openat, AT_FDCWD, filePath, O_CREAT | O_WRONLY | O_APPEND, 0644);

        if (fileFD == -1) {
            capio_syscall(SYS_write, fileno(stdout),
                          "Err fopen file: ", strlen("Err fopen file: "));
            capio_syscall(SYS_write, fileno(stdout), filePath, strlen(filePath));
            capio_syscall(SYS_write, fileno(stdout), " ", 1);
            capio_syscall(SYS_write, fileno(stdout), strerror(errno), strlen(strerror(errno)));
            capio_syscall(SYS_write, fileno(stdout), "\n", 1);
            exit(EXIT_FAILURE);
        }
        fileOpen = true;
    }

    static void write(const char * /*invoker*/, const char * /*file*/, unsigned int /*line*/,
                      long int /*tid*/, const char *buf, size_t len) {
        writeToFD(buf, len);
    }

    static void writeRaw(const char *buf, size_t len) { writeToFD(buf, len); }

    static bool isSTLSafe() { return false; }

    static void writeOpening() {

        writeRaw(CAPIO_LOG_POSIX_SYSCALL_START, strlen(CAPIO_LOG_POSIX_SYSCALL_START));
    }

    static void writeEpilogue() {
        writeRaw(CAPIO_LOG_POSIX_SYSCALL_END, strlen(CAPIO_LOG_POSIX_SYSCALL_END));
    }
};

using Logger = TemplateLogger<PosixLogWriteAdapter>;

#endif // CAPIO_POSIXLOGGER_HPP