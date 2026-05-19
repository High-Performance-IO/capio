#ifndef CAPIO_POSIXLOGGER_HPP
#define CAPIO_POSIXLOGGER_HPP

#include "common/logger.hpp"


struct PosixLogWriteAdapter {
  private:
    bool fileOpen{false};
    int fileFD{-1};
    char filePath[PATH_MAX]{'\0'};

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

    void setupLogFilename() {
        if (filePath[0] == '\0') {
            sprintf(filePath, "%s/%s%ld.log", getHostLogDir(), getLogPrefix(),
                    capio_syscall(SYS_gettid));
        }
    }

    // ---- write helper ------------------------------------------------------

    void writeToFD(const char *buf, size_t len) const {
        capio_syscall(SYS_write, fileFD, buf, len);
        capio_syscall(SYS_write, fileFD, "\n", 1);
    }

  public:
    [[nodiscard]] bool isFileOpened() const { return fileOpen; }

    void openLogFile() {
        setupLogFilename();
        current_log_level = 0; // reset log level after clone, avoiding propagation to child threads

        capio_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(), 0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getPosixLogDir(), 0755);
        capio_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(), 0755);

        fileFD = capio_syscall(SYS_openat, AT_FDCWD, filePath, O_CREAT | O_WRONLY | O_TRUNC, 0644);

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

    void write(const char * /*invoker*/, const char * /*file*/, unsigned int /*line*/,
               long int /*tid*/, const char *buf, size_t len) const {
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            writeToFD(buf, len);
        }
    }

    void writeRaw(const char *buf, size_t len) const {
        if (current_log_level < CAPIO_MAX_LOG_LEVEL || CAPIO_MAX_LOG_LEVEL < 0) {
            writeToFD(buf, len);
        }
    }

    void writeSyscallEnd() const {
        writeRaw(CAPIO_LOG_POSIX_SYSCALL_END, strlen(CAPIO_LOG_POSIX_SYSCALL_END));
    }

    static bool isServerInvoker(const char * /*invoker*/, const char * /*message*/) {
        return false;
    }
};

using Logger = TemplateLogger<PosixLogWriteAdapter>;

#endif // CAPIO_POSIXLOGGER_HPP
