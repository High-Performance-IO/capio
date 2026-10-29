#ifndef CAPIO_RUNTIME_CONFIGURATION_HPP
#define CAPIO_RUNTIME_CONFIGURATION_HPP
#include "calf/StlLogger.h"
#include "capiocl/configuration.h"
#include "common/constants.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <unistd.h>
#include <vector>

/**
 * @brief Effective CAPIO server configuration.
 *
 * Values are resolved, from lowest to highest priority, from: built-in defaults, the legacy CAPIO_*
 * environment variables, the TOML configuration file and the `-o key=value` command line
 * overrides. Every option is described by a single table in runtime_configuration.cpp, which is
 * also used to generate the documented template printed by `capio_server --genconf`.
 */
struct CapioParsedConfig {
    std::filesystem::path capio_dir;
    bool continue_on_error = false;

    long cache_lines           = CAPIO_CACHE_LINES_DEFAULT;
    long cache_line_size       = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    off64_t file_initial_size  = CAPIO_DEFAULT_FILE_INITIAL_SIZE;
    off64_t prefetch_data_size = 0;

    std::string discovery_interface = CAPIO_MCAST_PROTO_FLAG;
    std::string mcast_addr          = CAPIO_MCAST_ADV_DEFAULT_ADDR;
    unsigned int mcast_port         = CAPIO_MCAST_ADV_DEFAULT_PORT;
    std::string token_directory     = CAPIO_FS_DEFAULT_TOKEN_DIRECTORY;

    std::string backend_name  = "none";
    std::string mtcl_protocol = CAPIO_MTCL_DEFAULT_PROTOCOL;
    int mtcl_port             = CAPIO_MTCL_DEFAULT_PORT;
    int mtcl_poll_interval_us = CAPIO_MTCL_DEFAULT_POLL_INTERVAL;

    /// @brief Only the `capiocl.*` keys explicitly set by the user, forwarded verbatim to CAPIO-CL
    capiocl::configuration::CapioClConfiguration capio_cl_config;
};

/**
 * @brief Resolve the server configuration.
 * @param file Optional TOML configuration file.
 * @param overrides `key=value` strings, applied after the file, in order.
 * @throws std::runtime_error on unknown keys, wrongly typed values or invalid values.
 */
CapioParsedConfig load_config(const std::optional<std::filesystem::path> &file,
                              const std::vector<std::string> &overrides = {});

/// @brief Write a commented TOML template listing every option with its default value.
void write_config_template(std::ostream &output);

/// @brief Write the resolved configuration as flat TOML (`section.key = value`).
void write_effective_config(std::ostream &output, const std::optional<std::filesystem::path> &file,
                            const std::vector<std::string> &overrides = {});

/// @brief Parse the command line, load the configuration and exit on errors or info flags.
CapioParsedConfig parse_cli(int argc, char **argv);

inline CapioParsedConfig server_configuration;

inline void configure_server_runtime(const CapioParsedConfig &configuration) {
    server_configuration = configuration;
}

inline const std::filesystem::path &get_server_capio_dir() {
    return server_configuration.capio_dir;
}
inline long get_server_cache_lines() { return server_configuration.cache_lines; }
inline long get_server_cache_line_size() { return server_configuration.cache_line_size; }
inline off64_t get_file_initial_size() { return server_configuration.file_initial_size; }
inline off64_t get_prefetch_data_size() { return server_configuration.prefetch_data_size; }

/// @brief Server counterpart of is_capio_dir() in common/filesystem.hpp, which reads CAPIO_DIR from
/// the environment: the server checks against the configured capio.directory instead.
inline bool is_capio_dir(const std::filesystem::path &path_to_check) {
    START_LOG(gettid(), "call(path_to_check=%s)", path_to_check.c_str());

    const auto res = get_server_capio_dir().compare(path_to_check) == 0;
    LOG("is_capio_dir:%s", res ? "yes" : "no");
    return res;
}

#endif // CAPIO_RUNTIME_CONFIGURATION_HPP
