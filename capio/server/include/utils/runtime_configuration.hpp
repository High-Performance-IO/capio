#ifndef CAPIO_CLI_PARSER_HPP
#define CAPIO_CLI_PARSER_HPP
#include "common/constants.hpp"

#include <string>

struct CapioParsedConfig {
    std::string backend_name;
    std::string discovery_interface;
    std::string mcast_addr;
    unsigned int mcast_port;
    std::string token_directory;
    std::string backend_options;
    std::string capio_cl_config_path;
    std::string capio_cl_resolve_path;
    bool capio_cl_dynamic_config         = false;
    bool store_all_in_memory             = false;
    off64_t capio_file_default_init_size = CAPIO_DEFAULT_FILE_INITIAL_SIZE;
    off64_t capio_prefetch_data_size     = 0;
};

#endif // CAPIO_CLI_PARSER_HPP
