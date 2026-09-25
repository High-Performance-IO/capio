#ifndef CAPIO_RUNTIME_CONFIGURATION_HPP
#define CAPIO_RUNTIME_CONFIGURATION_HPP
#include "common/constants.hpp"
#include "capiocl/configuration.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <unordered_map>

using RuntimeConfigMap = std::unordered_map<std::string, std::string>;

struct CapioParsedConfig {
    std::filesystem::path capio_dir;
    std::string backend_name;
    std::string discovery_interface;
    std::string mcast_addr;
    unsigned int mcast_port = CAPIO_MCAST_ADV_DEFAULT_PORT;
    std::string token_directory;
    std::string backend_options;
    capiocl::configuration::CapioClConfiguration capio_cl_config;
    bool continue_on_error              = false;
    long cache_lines                    = CAPIO_CACHE_LINES_DEFAULT;
    long cache_line_size                = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    off64_t capio_file_default_init_size = CAPIO_DEFAULT_FILE_INITIAL_SIZE;
    off64_t capio_prefetch_data_size     = 0;
};

CapioParsedConfig parse_config(const std::filesystem::path &path);
CapioParsedConfig default_config();
CapioParsedConfig parse_cli(int argc, char **argv);
void write_default_config(std::ostream &output);

#endif // CAPIO_RUNTIME_CONFIGURATION_HPP
