#ifndef CAPIO_SERVERLOGGER_HPP
#define CAPIO_SERVERLOGGER_HPP

#include <common/logger.hpp>

struct ServerLogWriteAdapter {
  private:
    std::ofstream logfile;
    std::string logFileName;

    void writeToStream(const char *buf) {
        if (!logfile.is_open()) {
            return;
        }

        if (current_log_level < CAPIO_LOG_LEVEL || CAPIO_LOG_LEVEL < 0) {
            logfile << buf << std::endl;
            logfile.flush();
        }
    }

  public:
    void openLogFile() {
        if (this->logfile.is_open()) {
            return;
        }
        std::string logMasterDirName;
        std::string logfilePrefix;

        if (const char *tmp = std::getenv("CAPIO_LOG_DIR"); tmp == nullptr) {
            logMasterDirName = CAPIO_DEFAULT_LOG_FOLDER;
        } else {
            logMasterDirName = tmp;
        }

        if (const char *tmp = std::getenv("CAPIO_LOG_PREFIX"); tmp == nullptr) {
            logfilePrefix = CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX;
        } else {
            logfilePrefix = tmp;
        }

        char hostname[HOST_NAME_MAX];
        gethostname(hostname, HOST_NAME_MAX);

        const std::filesystem::path outputFolder{logMasterDirName + "/server/" + hostname};
        std::filesystem::create_directories(outputFolder);

        const std::filesystem::path logfileName = outputFolder.string() + "/" + logfilePrefix +
                                                  std::to_string(capio_syscall(SYS_gettid)) +
                                                  ".log";

        logfile.open(logfileName, std::ofstream::app);
        this->logFileName = logfileName;
    }

    void write(const char * /*invoker*/, const char * /*file*/, unsigned int /*line*/,
               long int /*tid*/, const char *buf, size_t /*len*/) {
        writeToStream(buf);
    }

    void writeRaw(const char *buf, size_t /*len*/) { writeToStream(buf); }

    static void writeSyscallEnd() {}

    static bool isSTLSafe() { return true; }

    const std::string &getLogFileName() { return logFileName; }
};

using Logger = TemplateLogger<ServerLogWriteAdapter>;

#endif // CAPIO_SERVERLOGGER_HPP
