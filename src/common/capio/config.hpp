#ifndef CAPIO_CONFIG_HPP
#define CAPIO_CONFIG_HPP
#include "constants.hpp"
#include <cstring>

#include <filesystem>

struct CapioConfig {
    std::string CAPIO_APP_NAME               = CAPIO_DEFAULT_APP_NAME;
    std::string CAPIO_WORKFLOW_NAME          = CAPIO_DEFAULT_WORKFLOW_NAME;
    std::string CAPIO_DIR                    = std::getenv("PWD");
    std::string CAPIO_LOG_PREFIX             = CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX;
    std::filesystem::path CAPIO_LOG_DIR      = CAPIO_DEFAULT_LOG_FOLDER;
    std::filesystem::path CAPIO_METADATA_DIR = CAPIO_DIR;
    size_t CAPIO_WRITE_CACHE_SIZE            = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    size_t CAPIO_CACHE_LINES                 = CAPIO_CACHE_LINES_DEFAULT;
    size_t CAPIO_CACHE_LINE_SIZE             = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    size_t CAPIO_POSIX_CACHE_LINE_SIZE       = CAPIO_CACHE_LINE_SIZE;
    int CAPIO_LOG_LEVEL                      = -1;
    bool CAPIO_STORE_ONLY_IN_MEMORY          = false;
    bool _initialized                        = false;
};

CapioConfig *capio_config;

void fill_capio_cofiguration() {
    auto capio_app_name      = std::getenv("CAPIO_APP_NAME");
    auto capio_workflow_name = std::getenv("CAPIO_WORKFLOW_NAME");

    capio_config->_initialized = true;
}

#endif // CAPIO_CONFIG_HPP
