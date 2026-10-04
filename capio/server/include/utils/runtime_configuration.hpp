#ifndef CAPIO_RUNTIME_CONFIGURATION_HPP
#define CAPIO_RUNTIME_CONFIGURATION_HPP
#include "capiocl/configuration.h"
#include "common/constants.hpp"

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
    bool continue_on_error               = false;
    long cache_lines                     = CAPIO_CACHE_LINES_DEFAULT;
    long cache_line_size                 = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    off64_t capio_file_default_init_size = CAPIO_DEFAULT_FILE_INITIAL_SIZE;
    off64_t capio_prefetch_data_size     = 0;
};

CapioParsedConfig parse_config(const std::filesystem::path &path);
CapioParsedConfig default_config();
CapioParsedConfig parse_cli(int argc, char **argv);
void write_default_config(std::ostream &output);

inline std::filesystem::path server_capio_dir = std::filesystem::current_path();
inline long server_cache_lines                = CAPIO_CACHE_LINES_DEFAULT;
inline long server_cache_line_size            = CAPIO_CACHE_LINE_SIZE_DEFAULT;
inline off64_t server_file_initial_size       = CAPIO_DEFAULT_FILE_INITIAL_SIZE;
inline off64_t server_prefetch_data_size      = 0;

inline void configure_server_runtime(const std::filesystem::path &capio_dir, long cache_lines,
                                     long cache_line_size, off64_t file_initial_size,
                                     off64_t prefetch_data_size) {
    server_capio_dir          = capio_dir;
    server_cache_lines        = cache_lines;
    server_cache_line_size    = cache_line_size;
    server_file_initial_size  = file_initial_size;
    server_prefetch_data_size = prefetch_data_size;
}

inline const std::filesystem::path &get_server_capio_dir() { return server_capio_dir; }
inline long get_server_cache_lines() { return server_cache_lines; }
inline long get_server_cache_line_size() { return server_cache_line_size; }
inline off64_t get_file_initial_size() { return server_file_initial_size; }
inline off64_t get_prefetch_data_size() { return server_prefetch_data_size; }

#endif // CAPIO_RUNTIME_CONFIGURATION_HPP
