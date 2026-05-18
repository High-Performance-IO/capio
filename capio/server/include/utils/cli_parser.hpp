#ifndef CAPIO_CLI_PARSER_HPP
#define CAPIO_CLI_PARSER_HPP
#include <string>

struct CapioParsedConfig {
    std::string backend_name;
    std::string capio_cl_config_path;
    std::string capio_cl_resolve_path;
    bool capio_cl_dynamic_config = false;
    bool store_all_in_memory     = false;
};

CapioParsedConfig parseCLI(int argc, char **argv);

#endif // CAPIO_CLI_PARSER_HPP
