#ifndef CAPIO_SERVERLOGGER_HPP
#define CAPIO_SERVERLOGGER_HPP

#include "common/logger.hpp"
#include "common/json_base_logger.hpp"

struct ServerLogWriteAdapter : JsonLogBase<ServerLogWriteAdapter> {

    static thread_local std::ofstream *logfile;
    static thread_local std::string   *logFileName;

    explicit ServerLogWriteAdapter() { ensureFileOpen(); }

    std::string getLogFileName() const {
        return logFileName ? *logFileName : std::string{};
    }

    // ------------------------------------------------------------------ //
    //  I/O primitives required by JsonLogBase
    // ------------------------------------------------------------------ //

    static void rawWriteBytes(const char *buf, int len) {
        ensureFileOpen();
        logfile->write(buf, len);
        logfile->flush();
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(strlen(buf)));
    }

  private:
    static void ensureFileOpen() {
        if (logfile != nullptr && logfile->is_open()) { return; }

        std::string logDir;
        std::string prefix;

        if (const char *tmp = std::getenv("CAPIO_LOG_DIR"); tmp != nullptr) {
            logDir = tmp;
        } else {
            logDir = CAPIO_DEFAULT_LOG_FOLDER;
        }

        if (const char *tmp = std::getenv("CAPIO_LOG_PREFIX"); tmp != nullptr) {
            prefix = tmp;
        } else {
            prefix = CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX;
        }

        char hostname[HOST_NAME_MAX];
        gethostname(hostname, HOST_NAME_MAX);

        const std::filesystem::path outputFolder{logDir + "/server/" + hostname};
        std::filesystem::create_directories(outputFolder);

        const std::filesystem::path path =
            outputFolder / (prefix + std::to_string(capio_syscall(SYS_gettid)) + ".log");

        logfile     = new std::ofstream(path, std::ofstream::app);
        logFileName = new std::string(path.string());
    }
};

inline thread_local std::ofstream *ServerLogWriteAdapter::logfile     = nullptr;
inline thread_local std::string   *ServerLogWriteAdapter::logFileName = nullptr;

using Logger = TemplateLogger<ServerLogWriteAdapter>;

#endif // CAPIO_SERVERLOGGER_HPP