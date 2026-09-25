#include "utils/runtime_configuration.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "utils/common.hpp"

#include "toml++/toml.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

void flatten(const toml::table &table, RuntimeConfigMap &config, const std::string &prefix = "") {
    for (const auto &[key, value] : table) {
        const auto full_key =
            prefix.empty() ? std::string{key.str()} : prefix + "." + std::string{key.str()};
        if (const auto *nested = value.as_table()) {
            flatten(*nested, config, full_key);
        } else if (value.is_string()) {
            config[full_key] = value.as_string()->get();
        } else if (value.is_boolean()) {
            config[full_key] = value.as_boolean()->get() ? "true" : "false";
        } else if (value.is_integer()) {
            config[full_key] = std::to_string(value.as_integer()->get());
        } else {
            throw std::runtime_error("unsupported TOML value for '" + full_key + "'");
        }
    }
}

std::string get(const RuntimeConfigMap &config, const std::string &key,
                const std::string &fallback = "") {
    const auto value = config.find(key);
    return value == config.end() ? fallback : value->second;
}

bool get_bool(const RuntimeConfigMap &config, const std::string &key, bool fallback = false) {
    const auto value = get(config, key, fallback ? "true" : "false");
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    throw std::runtime_error("invalid boolean for '" + key + "'");
}

long long get_integer(const RuntimeConfigMap &config, const std::string &key, long long fallback) {
    const auto value = config.find(key);
    return value == config.end() ? fallback : std::stoll(value->second);
}

void write_comment(std::ostream &output, std::string_view comment) {
    constexpr size_t max_content_length = 118; // Account for the "# " prefix.
    while (!comment.empty()) {
        const auto newline = comment.find('\n');
        auto line          = comment.substr(0, newline);
        while (line.size() > max_content_length) {
            auto split = line.rfind(' ', max_content_length);
            if (split == std::string_view::npos || split == 0) {
                split = max_content_length;
            }
            output << "# " << line.substr(0, split) << '\n';
            line.remove_prefix(split);
            while (!line.empty() && line.front() == ' ') {
                line.remove_prefix(1);
            }
        }
        while (!line.empty() && line.back() == ' ') {
            line.remove_suffix(1);
        }
        output << "# " << line << '\n';
        if (newline == std::string_view::npos) {
            break;
        }
        comment.remove_prefix(newline + 1);
    }
}

void write_usage(std::ostream &output) {
    output << "Usage:\n"
              "  capio_server <config.toml>  Start with a TOML configuration file\n"
              "  capio_server --defconf      Start with built-in defaults\n"
              "  capio_server --genconf      Write built-in defaults to ./default.toml\n";
}

} // namespace

void write_default_config(std::ostream &output) {
    output << "[capiocl]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP);
    output << "workflow_name = \"CAPIO\"\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP);
    output << "config_path = \"\"\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_RESOLVE_PREFIX_OPT_HELP);
    output << "resolve_path = \"\"\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_STORE_ALL_IN_MEMORY_OPT_HELP);
    output << "store_all_in_memory = false\n\n"
              "[capiocl.monitor.mcast]\n"
              "enabled = true\n"
              "delay_ms = 300\n"
              "commit = { ip = \"224.224.224.1\", port = 12345 }\n"
              "homenode = { ip = \"224.224.224.2\", port = 12345 }\n\n"
              "[capiocl.monitor.filesystem]\n"
              "enabled = true\n\n"
              "[capiocl.dynamic_api]\n"
              "enabled = false\n"
              "ip = \"224.224.224.3\"\n"
              "port = 11223\n\n"
              "[capio]\n";
    output << "# Root directory managed by CAPIO.\n"
              "directory = \".\"\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP);
    output << "continue_on_error = false\n\n"
              "[capio.storage]\n"
              "file_initial_size = "
           << CAPIO_DEFAULT_FILE_INITIAL_SIZE
           << "\n"
              "prefetch_data_size = 0\n\n"
              "[capio.cache]\n"
              "lines = "
           << CAPIO_CACHE_LINES_DEFAULT << "\nline_size = " << CAPIO_CACHE_LINE_SIZE_DEFAULT
           << "\n\n"
              "[capio.discovery_service]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_DISCOVERY_HELP);
    output << "type = \"mcast\"\n"
              "interval_ms = 1000\n\n"
              "[capio.discovery_service.mcast]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_MCAST_ADDR_HELP);
    output << "addr = \"" << CAPIO_MCAST_ADV_DEFAULT_ADDR << "\"\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_MCAST_PORT_HELP);
    output << "port = " << CAPIO_MCAST_ADV_DEFAULT_PORT
           << "\n\n"
              "[capio.discovery_service.fs]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_TOKEN_DIRECTORY_HELP);
    output << "token_directory = \".capio_tokens/\"\n\n"
              "[capio.backend]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP);
    output << "type = \"none\"\n\n"
              "[capio.backend.mtcl]\n";
    write_comment(output, CAPIO_SERVER_ARG_PARSER_BACKEND_OPTIONS_HELP);
    output << "proto = \"" << CAPIO_MTCL_DEFAULT_PROTOCOL
           << "\"\n"
              "listen_address = \"0.0.0.0\"\n"
              "port = "
           << CAPIO_MTCL_DEFAULT_PORT << "\npoll_interval_us = " << CAPIO_MTCL_DEFAULT_POLL_INTERVAL
           << '\n';
}

static CapioParsedConfig parse_table(const toml::table &table) {
    RuntimeConfigMap flattened;
    flatten(table, flattened);

    CapioParsedConfig result;
    RuntimeConfigMap capio_cl_config;
    constexpr char capiocl_prefix[] = "capiocl.";
    for (const auto &[key, value] : flattened) {
        if (key.compare(0, sizeof(capiocl_prefix) - 1, capiocl_prefix) == 0) {
            capio_cl_config.emplace(key, value);
        }
    }
    result.capio_cl_config =
        capiocl::configuration::CapioClConfiguration(std::move(capio_cl_config));

    const auto capio_dir = get(flattened, "capio.directory", ".");
    if (!std::filesystem::is_directory(capio_dir)) {
        throw std::runtime_error("capio.directory must be an existing directory");
    }
    result.capio_dir = std::filesystem::canonical(capio_dir);

    result.discovery_interface =
        get(flattened, "capio.discovery_service.type", CAPIO_MCAST_PROTO_FLAG);
    if (result.discovery_interface != CAPIO_MCAST_PROTO_FLAG &&
        result.discovery_interface != CAPIO_FS_PROTO_FLAG) {
        throw std::runtime_error("capio.discovery_service.type must be 'mcast' or 'fs'");
    }
    result.mcast_addr =
        get(flattened, "capio.discovery_service.mcast.addr", CAPIO_MCAST_ADV_DEFAULT_ADDR);
    result.mcast_port = static_cast<unsigned int>(
        get_integer(flattened, "capio.discovery_service.mcast.port", CAPIO_MCAST_ADV_DEFAULT_PORT));
    result.token_directory =
        get(flattened, "capio.discovery_service.fs.token_directory", ".capio_tokens/");

    result.backend_name = get(flattened, "capio.backend.type", "none");
    const auto protocol = get(flattened, "capio.backend.mtcl.proto", CAPIO_MTCL_DEFAULT_PROTOCOL);
    const auto port     = get(flattened, "capio.backend.mtcl.port", CAPIO_MTCL_DEFAULT_PORT);
    const auto poll_interval = get_integer(flattened, "capio.backend.mtcl.poll_interval_us",
                                           CAPIO_MTCL_DEFAULT_POLL_INTERVAL);
    result.backend_options   = protocol + ":" + port + "@" + std::to_string(poll_interval);

    result.capio_file_default_init_size =
        get_integer(flattened, "capio.storage.file_initial_size", CAPIO_DEFAULT_FILE_INITIAL_SIZE);
    result.capio_prefetch_data_size = get_integer(flattened, "capio.storage.prefetch_data_size", 0);
    result.cache_lines = get_integer(flattened, "capio.cache.lines", CAPIO_CACHE_LINES_DEFAULT);
    result.cache_line_size =
        get_integer(flattened, "capio.cache.line_size", CAPIO_CACHE_LINE_SIZE_DEFAULT);
    if (result.cache_lines <= 0 || result.cache_line_size <= 0 ||
        result.capio_file_default_init_size <= 0 || result.capio_prefetch_data_size < 0) {
        throw std::runtime_error("CAPIO storage and cache sizes must be positive");
    }
    result.continue_on_error = get_bool(flattened, "capio.continue_on_error");
    return result;
}

CapioParsedConfig parse_config(const std::filesystem::path &path) {
    return parse_table(toml::parse_file(path.string()));
}

CapioParsedConfig default_config() {
    std::ostringstream output;
    write_default_config(output);
    return parse_table(toml::parse(output.str()));
}

CapioParsedConfig parse_cli(int argc, char **argv) {
    if (argc == 2 && std::string{argv[1]} == "--genconf") {
        std::ofstream output("default.toml");
        if (!output) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Failed to create default.toml");
            std::exit(EXIT_FAILURE);
        }
        write_default_config(output);
        output.close();
        if (!output) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Failed to write default.toml");
            std::exit(EXIT_FAILURE);
        }
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Generated default.toml");
        std::exit(EXIT_SUCCESS);
    }
    if (argc != 2 || std::string{argv[1]} == "--help") {
        if (argc == 1) {
            std::cerr << "No configuration provided. Choose a TOML file or a default mode.\n\n";
        } else if (argc != 2) {
            std::cerr << "Expected exactly one argument.\n\n";
        }
        write_usage(argc == 2 ? std::cout : std::cerr);
        std::exit(argc == 2 ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    CapioParsedConfig config;
    if (std::string{argv[1]} == "--defconf") {
        config = default_config();
    } else {
        try {
            config = parse_config(argv[1]);
        } catch (const std::exception &error) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Failed to load configuration '%s': %s", argv[1],
                             error.what());
            std::exit(EXIT_FAILURE);
        }
    }

    if (config.continue_on_error) {
#ifdef CAPIO_LOG
        continue_on_error = true;
        for (const auto line : CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING) {
            CALF_PRINT("%s", line);
        }
#else
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "capio.continue_on_error is enabled, but logging is not compiled in");
#endif
    }

#ifdef CAPIO_LOG
    for (const auto line : CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "%s", line);
    }
    auto log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "started logging to logfile %s",
                     log->getLogFileName().c_str());
#endif

    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "CAPIO directory=%s", config.capio_dir.c_str());
    return config;
}
