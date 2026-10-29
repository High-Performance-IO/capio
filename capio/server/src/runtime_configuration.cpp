#include "utils/runtime_configuration.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "utils/common.hpp"

#include "toml++/toml.hpp"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace {

using Value    = std::variant<bool, long long, std::string>;
using ValueMap = std::map<std::string, Value>;

enum class Kind { BOOL, INTEGER, STRING };

/// @brief Description of a single configuration option. This table is the single source of truth
/// for defaults, validation, documentation and the generated template.
struct Option {
    std::string key;
    Value default_value;
    std::string help;
    /// @brief Legacy environment variable that, if set, replaces the built-in default
    const char *env                  = nullptr;
    std::vector<std::string> choices = {};
    long long min                    = LLONG_MIN;
    long long max                    = LLONG_MAX;
};

constexpr std::string_view CAPIO_SECTION   = "capio";
constexpr std::string_view CAPIOCL_SECTION = "capiocl";

const std::vector<Option> &options() {
    static const std::vector<Option> table = {
        // CAPIO server
        {"capio.directory", std::string{"."},
         "Root directory managed by CAPIO. Applications must be started with the same value in "
         "CAPIO_DIR.",
         "CAPIO_DIR"},
        {"capio.continue_on_error", false,
         "Debug only: keep running after a fatal error. Requires a build with -DCAPIO_LOG=TRUE. "
         "Behaviour after a fatal error is undefined."},
        {"capio.storage.file_initial_size",
         static_cast<long long>(CAPIO_DEFAULT_FILE_INITIAL_SIZE),
         "Bytes preallocated for each new file. Larger values reduce reallocations but use more "
         "memory.",
         "CAPIO_FILE_INIT_SIZE",
         {},
         1},
        {"capio.storage.prefetch_data_size",
         0LL,
         "Minimum size in bytes of each data transfer between nodes (0 = no prefetching).",
         "CAPIO_PREFETCH_DATA_SIZE",
         {},
         0},
        {"capio.cache.lines",
         static_cast<long long>(CAPIO_CACHE_LINES_DEFAULT),
         "Number of cache lines shared with each application. Applications must be started with "
         "the same value in CAPIO_CACHE_LINES.",
         "CAPIO_CACHE_LINES",
         {},
         1},
        {"capio.cache.line_size",
         static_cast<long long>(CAPIO_CACHE_LINE_SIZE_DEFAULT),
         "Size in bytes of a cache line. Applications must be started with the same value in "
         "CAPIO_CACHE_LINE_SIZE.",
         "CAPIO_CACHE_LINE_SIZE",
         {},
         1},
        {"capio.discovery.type",
         std::string{CAPIO_MCAST_PROTO_FLAG},
         "How CAPIO servers find each other: 'mcast' (UDP multicast) or 'fs' (token files in a "
         "shared directory).",
         nullptr,
         {CAPIO_MCAST_PROTO_FLAG, CAPIO_FS_PROTO_FLAG}},
        {"capio.discovery.mcast.address", std::string{CAPIO_MCAST_ADV_DEFAULT_ADDR},
         "Multicast group used by the 'mcast' discovery."},
        {"capio.discovery.mcast.port",
         static_cast<long long>(CAPIO_MCAST_ADV_DEFAULT_PORT),
         "UDP port used by the 'mcast' discovery.",
         nullptr,
         {},
         1,
         65535},
        {"capio.discovery.fs.token_directory", std::string{CAPIO_FS_DEFAULT_TOKEN_DIRECTORY},
         "Directory, shared among all nodes, used by the 'fs' discovery."},
        {"capio.backend.type",
         std::string{"none"},
         "Backend used to communicate with other CAPIO servers. 'none' runs a single node.",
         nullptr,
#ifdef CAPIO_HAS_MPI
         {"none", "mtcl", "mpi", "mpisync"}
#else
         {"none", "mtcl"}
#endif
        },
        {"capio.backend.mtcl.protocol", std::string{CAPIO_MTCL_DEFAULT_PROTOCOL},
         "MTCL protocol, see https://github.com/ParaGroup/MTCL for the supported ones."},
        {"capio.backend.mtcl.port",
         static_cast<long long>(CAPIO_MTCL_DEFAULT_PORT),
         "Port on which MTCL listens for other CAPIO servers.",
         nullptr,
         {},
         1,
         65535},
        {"capio.backend.mtcl.poll_interval_us",
         static_cast<long long>(CAPIO_MTCL_DEFAULT_POLL_INTERVAL),
         "Microseconds between MTCL polls. Also sets the discovery advertisement period.",
         nullptr,
         {},
         1,
         INT_MAX},

        // CAPIO-CL. These keys are forwarded to CAPIO-CL only when set; defaults are CAPIO-CL's.
        {"capiocl.config_path", std::string{""},
         "CAPIO-CL JSON file describing the workflow. If empty, CAPIO runs without a workflow "
         "description."},
        {"capiocl.resolve_path", std::string{""},
         "Prefix used to resolve relative paths found in the CAPIO-CL JSON file."},
        {"capiocl.workflow_name", std::string{CAPIO_DEFAULT_WORKFLOW_NAME},
         "Workflow name. If set, it overrides the name in the CAPIO-CL JSON file. Applications "
         "must "
         "be started with the same value in CAPIO_WORKFLOW_NAME."},
        {"capiocl.store_all_in_memory", false,
         "Keep every file in memory, ignoring the storage policy of the CAPIO-CL JSON file."},
        {"capiocl.dynamic_api.enabled", false,
         "Start the CAPIO-CL API server to change the workflow configuration at runtime."},
        {"capiocl.dynamic_api.ip", std::string{"224.224.224.3"}, "Multicast group of the API."},
        {"capiocl.dynamic_api.port", 11223LL, "Port of the API.", nullptr, {}, 1, 65535},
        {"capiocl.monitor.mcast.enabled", false,
         "CAPIO-CL commit monitors share the commit state of files among CAPIO-CL engines (e.g. "
         "other tools using CAPIO-CL). The CAPIO server does not need them: it tracks commits "
         "itself. They are disabled unless explicitly enabled here. When enabled, this monitor "
         "starts background threads exchanging commit and home node messages over UDP "
         "multicast."},
        {"capiocl.monitor.mcast.delay_ms",
         300LL,
         "Milliseconds between multicast monitor operations.",
         nullptr,
         {},
         0},
        {"capiocl.monitor.mcast.commit.ip", std::string{"224.224.224.1"},
         "Multicast group and port used to exchange commit messages."},
        {"capiocl.monitor.mcast.commit.port", 12345LL, "", nullptr, {}, 1, 65535},
        {"capiocl.monitor.mcast.homenode.ip", std::string{"224.224.224.2"},
         "Multicast group and port used to exchange home node messages."},
        {"capiocl.monitor.mcast.homenode.port", 12345LL, "", nullptr, {}, 1, 65535},
        {"capiocl.monitor.filesystem.enabled", false,
         "Disabled unless explicitly enabled here. When enabled, commits and home nodes are "
         "recorded as token files, so the filesystem must be shared among nodes."},
        {"capiocl.monitor.filesystem.metadata_dir", std::string{""},
         "Directory, shared among nodes, where the filesystem monitor stores its token files. "
         "Required by CAPIO-CL to count on_close commits with this monitor."},
    };
    return table;
}

const Option *find_option(const std::string &key) {
    const auto &table = options();
    const auto it =
        std::find_if(table.begin(), table.end(), [&](const Option &o) { return o.key == key; });
    return it == table.end() ? nullptr : &*it;
}

Kind kind_of(const Value &value) { return static_cast<Kind>(value.index()); }

const char *kind_name(Kind kind) {
    switch (kind) {
    case Kind::BOOL:
        return "a boolean";
    case Kind::INTEGER:
        return "an integer";
    default:
        return "a string";
    }
}

std::string section_of(const std::string &key) { return key.substr(0, key.find('.')); }

std::string to_string(const Value &value) {
    if (const auto *b = std::get_if<bool>(&value)) {
        return *b ? "true" : "false";
    }
    if (const auto *i = std::get_if<long long>(&value)) {
        return std::to_string(*i);
    }
    return std::get<std::string>(value);
}

std::string to_toml(const Value &value) {
    if (const auto *s = std::get_if<std::string>(&value)) {
        std::ostringstream output;
        output << toml::value<std::string>(*s);
        return output.str();
    }
    return to_string(value);
}

/// @brief Validate @p value against the option table and store it under @p key.
void set_value(ValueMap &values, const std::string &key, Value value, const std::string &origin) {
    const auto section = section_of(key);
    if (section != CAPIO_SECTION && section != CAPIOCL_SECTION) {
        throw std::runtime_error(origin + ": unknown key '" + key +
                                 "' (keys must start with 'capio.' or 'capiocl.')");
    }
    const auto *option = find_option(key);
    if (option == nullptr) {
        if (section == CAPIO_SECTION) {
            throw std::runtime_error(origin + ": unknown key '" + key +
                                     "' (run 'capio_server --genconf' to list valid keys)");
        }
        // Accept unknown CAPIO-CL keys so newer CAPIO-CL releases can be configured as well
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "%s: unknown CAPIO-CL key '%s' forwarded as is",
                         origin.c_str(), key.c_str());
        values[key] = std::move(value);
        return;
    }

    const auto expected = kind_of(option->default_value);
    if (kind_of(value) != expected) {
        throw std::runtime_error(origin + ": '" + key + "' must be " + kind_name(expected));
    }
    if (const auto *i = std::get_if<long long>(&value);
        i && (*i < option->min || *i > option->max)) {
        throw std::runtime_error(origin + ": '" + key + "' must be between " +
                                 std::to_string(option->min) + " and " +
                                 std::to_string(option->max));
    }
    if (!option->choices.empty()) {
        const auto &s = std::get<std::string>(value);
        if (std::find(option->choices.begin(), option->choices.end(), s) == option->choices.end()) {
            std::string allowed;
            for (const auto &choice : option->choices) {
                allowed += (allowed.empty() ? "'" : ", '") + choice + "'";
            }
            throw std::runtime_error(origin + ": invalid value '" + s + "' for '" + key +
                                     "' (allowed: " + allowed + ")");
        }
    }
    values[key] = std::move(value);
}

/// @brief Convert a textual value (environment variable or command line) to the type of @p key.
Value parse_text(const std::string &key, const std::string &text, const std::string &origin) {
    const auto *option = find_option(key);
    if (option == nullptr) {
        return text;
    }
    switch (kind_of(option->default_value)) {
    case Kind::BOOL:
        if (text == "true" || text == "false") {
            return text == "true";
        }
        throw std::runtime_error(origin + ": '" + key + "' must be true or false, got '" + text +
                                 "'");
    case Kind::INTEGER: {
        long long number        = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (error != std::errc() || end != text.data() + text.size()) {
            throw std::runtime_error(origin + ": '" + key + "' must be an integer, got '" + text +
                                     "'");
        }
        return number;
    }
    default:
        return text;
    }
}

void load_table(ValueMap &values, const toml::table &table, const std::string &origin,
                const std::string &prefix = "") {
    for (const auto &[name, node] : table) {
        const auto key =
            prefix.empty() ? std::string{name.str()} : prefix + "." + std::string{name.str()};
        if (const auto *nested = node.as_table()) {
            load_table(values, *nested, origin, key);
        } else if (const auto *s = node.as_string()) {
            set_value(values, key, s->get(), origin);
        } else if (const auto *b = node.as_boolean()) {
            set_value(values, key, b->get(), origin);
        } else if (const auto *i = node.as_integer()) {
            set_value(values, key, static_cast<long long>(i->get()), origin);
        } else {
            throw std::runtime_error(origin + ": unsupported value type for '" + key + "'");
        }
    }
}

ValueMap resolve(const std::optional<std::filesystem::path> &file,
                 const std::vector<std::string> &overrides) {
    ValueMap values;
    for (const auto &option : options()) {
        if (section_of(option.key) != CAPIO_SECTION) {
            continue; // CAPIO-CL applies its own defaults to keys that are not set
        }
        values[option.key] = option.default_value;
        if (option.env != nullptr) {
            if (const char *env = std::getenv(option.env); env != nullptr && *env != '\0') {
                set_value(values, option.key, parse_text(option.key, env, option.env),
                          std::string{"environment variable "} + option.env);
            }
        }
    }

    if (file) {
        toml::table table;
        try {
            table = toml::parse_file(file->string());
        } catch (const toml::parse_error &error) {
            std::ostringstream message;
            message << file->string() << ":" << error.source().begin.line << ":"
                    << error.source().begin.column << ": " << error.description();
            throw std::runtime_error(message.str());
        }
        load_table(values, table, file->string());
    }

    for (const auto &assignment : overrides) {
        const auto equal = assignment.find('=');
        if (equal == std::string::npos || equal == 0) {
            throw std::runtime_error("invalid override '" + assignment + "', expected key=value");
        }
        const auto key    = assignment.substr(0, equal);
        const auto origin = "override '" + assignment + "'";
        set_value(values, key, parse_text(key, assignment.substr(equal + 1), origin), origin);
    }
    return values;
}

template <typename T> T get(const ValueMap &values, const std::string &key) {
    return std::get<T>(values.at(key));
}

void write_comment(std::ostream &output, std::string_view comment, std::string_view indent = "") {
    constexpr size_t max_line_length = 100;
    const auto width                 = max_line_length - indent.size() - 2;
    while (!comment.empty()) {
        auto split = comment.size() <= width ? comment.size() : comment.rfind(' ', width);
        if (split == std::string_view::npos || split == 0) {
            split = std::min(width, comment.size());
        }
        output << indent << "# " << comment.substr(0, split) << '\n';
        comment.remove_prefix(split);
        while (!comment.empty() && comment.front() == ' ') {
            comment.remove_prefix(1);
        }
    }
}

void write_usage(std::ostream &output) {
    output
        << "Usage: capio_server [CONFIG.toml] [-o KEY=VALUE]...\n"
           "\n"
           "Start a CAPIO server. Settings are taken, from lowest to highest priority, from the\n"
           "built-in defaults, the legacy CAPIO_* environment variables, CONFIG.toml and the\n"
           "-o overrides.\n"
           "\n"
           "Options:\n"
           "  -o, --set KEY=VALUE  Override one setting, e.g. -o capio.backend.type=mtcl.\n"
           "                       May be repeated.\n"
           "  --genconf            Print a documented configuration template and exit.\n"
           "  --print-config       Print the resolved configuration and exit.\n"
           "  -h, --help           Print this help and exit.\n"
           "\n"
           "Example:\n"
           "  capio_server --genconf > capio.toml   # then edit capio.toml\n"
           "  capio_server capio.toml -o capio.directory=/tmp/capio\n"
           "\n"
           "Documentation: https://github.com/High-Performance-IO/capio\n";
}

[[noreturn]] void usage_error(const std::string &message) {
    std::cerr << "capio_server: " << message << "\n\n";
    write_usage(std::cerr);
    std::exit(EXIT_FAILURE);
}

/// @brief Warn when a value shared with applications differs from the environment of the server,
/// as applications usually inherit the same environment.
void check_application_environment(const CapioParsedConfig &config) {
    const auto warn_if_different = [](const char *env, const std::string &key,
                                      const std::string &value) {
        if (const char *current = std::getenv(env); current != nullptr && value != current) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                             "%s=%s differs from %s=%s: applications must use the server value",
                             env, current, key.c_str(), value.c_str());
        }
    };
    if (const char *env_dir = std::getenv("CAPIO_DIR"); env_dir != nullptr) {
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(env_dir, error);
        if (error || canonical != config.capio_dir) {
            warn_if_different("CAPIO_DIR", "capio.directory", config.capio_dir.string());
        }
    }
    warn_if_different("CAPIO_CACHE_LINES", "capio.cache.lines", std::to_string(config.cache_lines));
    warn_if_different("CAPIO_CACHE_LINE_SIZE", "capio.cache.line_size",
                      std::to_string(config.cache_line_size));
}

} // namespace

CapioParsedConfig load_config(const std::optional<std::filesystem::path> &file,
                              const std::vector<std::string> &overrides) {
    const auto values = resolve(file, overrides);

    CapioParsedConfig config;
    const std::filesystem::path capio_dir = get<std::string>(values, "capio.directory");
    if (!std::filesystem::is_directory(capio_dir)) {
        throw std::runtime_error("capio.directory '" + capio_dir.string() +
                                 "' is not an existing directory");
    }
    config.capio_dir          = std::filesystem::canonical(capio_dir);
    config.continue_on_error  = get<bool>(values, "capio.continue_on_error");
    config.file_initial_size  = get<long long>(values, "capio.storage.file_initial_size");
    config.prefetch_data_size = get<long long>(values, "capio.storage.prefetch_data_size");
    config.cache_lines        = get<long long>(values, "capio.cache.lines");
    config.cache_line_size    = get<long long>(values, "capio.cache.line_size");

    config.discovery_interface = get<std::string>(values, "capio.discovery.type");
    config.mcast_addr          = get<std::string>(values, "capio.discovery.mcast.address");
    config.mcast_port          = get<long long>(values, "capio.discovery.mcast.port");
    config.token_directory     = get<std::string>(values, "capio.discovery.fs.token_directory");

    config.backend_name          = get<std::string>(values, "capio.backend.type");
    config.mtcl_protocol         = get<std::string>(values, "capio.backend.mtcl.protocol");
    config.mtcl_port             = get<long long>(values, "capio.backend.mtcl.port");
    config.mtcl_poll_interval_us = get<long long>(values, "capio.backend.mtcl.poll_interval_us");

    std::unordered_map<std::string, std::string> capio_cl_values;
    for (const auto &[key, value] : values) {
        if (section_of(key) == CAPIOCL_SECTION) {
            capio_cl_values.emplace(key, to_string(value));
        }
    }
    config.capio_cl_config =
        capiocl::configuration::CapioClConfiguration(std::move(capio_cl_values));
    return config;
}

void write_config_template(std::ostream &output) {
    output
        << "# CAPIO server configuration.\n"
           "# Every option is commented out and shows its default value: uncomment and edit only\n"
           "# the ones you need. Any option can also be set from the command line with\n"
           "# 'capio_server -o key=value', e.g. -o capio.backend.type=mtcl\n";
    std::string current_table;
    for (const auto &option : options()) {
        const auto dot   = option.key.rfind('.');
        const auto table = option.key.substr(0, dot);
        if (table != current_table) {
            output << "\n[" << table << "]\n";
            if (table == CAPIOCL_SECTION) {
                write_comment(output, "Options forwarded to CAPIO-CL. When unset, CAPIO-CL "
                                      "applies its own default.");
            }
            current_table = table;
        }
        auto help = option.help;
        if (option.env != nullptr) {
            help += (help.empty() ? "" : " ") + std::string{"Defaults to $"} + option.env +
                    " when set.";
        }
        write_comment(output, help);
        output << "# " << option.key.substr(dot + 1) << " = " << to_toml(option.default_value)
               << '\n';
    }
}

void write_effective_config(std::ostream &output, const std::optional<std::filesystem::path> &file,
                            const std::vector<std::string> &overrides) {
    for (const auto &[key, value] : resolve(file, overrides)) {
        output << key << " = " << to_toml(value) << '\n';
    }
}

CapioParsedConfig parse_cli(int argc, char **argv) {
    std::optional<std::filesystem::path> file;
    std::vector<std::string> overrides;
    bool print_config = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            write_usage(std::cout);
            std::exit(EXIT_SUCCESS);
        } else if (argument == "--genconf") {
            write_config_template(std::cout);
            std::exit(EXIT_SUCCESS);
        } else if (argument == "--print-config") {
            print_config = true;
        } else if (argument == "-o" || argument == "--set") {
            if (i + 1 == argc) {
                usage_error(argument + " requires a KEY=VALUE argument");
            }
            overrides.emplace_back(argv[++i]);
        } else if (argument.rfind("--set=", 0) == 0) {
            overrides.emplace_back(argument.substr(6));
        } else if (!argument.empty() && argument.front() == '-') {
            usage_error("unknown option '" + argument + "'");
        } else if (file) {
            usage_error("only one configuration file can be given");
        } else {
            file = argument;
        }
    }

    CapioParsedConfig config;
    try {
        if (print_config) {
            write_effective_config(std::cout, file, overrides);
            std::exit(EXIT_SUCCESS);
        }
        config = load_config(file, overrides);
    } catch (const std::exception &error) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Invalid configuration: %s", error.what());
        std::exit(EXIT_FAILURE);
    }

    if (file) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Loaded configuration from %s",
                         file->string().c_str());
    } else {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "No configuration file given: starting with the built-in defaults");
        bool from_environment = false;
        for (const auto &option : options()) {
            if (const char *env = option.env ? std::getenv(option.env) : nullptr;
                env != nullptr && *env != '\0') {
                CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "  %s taken from %s=%s",
                                 option.key.c_str(), option.env, env);
                from_environment = true;
            }
        }
        if (!from_environment) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                             "  no CAPIO_* environment variable set: capio.directory is the "
                             "current directory");
        }
        if (!overrides.empty()) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "  %zu setting(s) overridden with -o",
                             overrides.size());
        }
        std::string workflow_file, mcast_monitor, fs_monitor;
        config.capio_cl_config.getParameter("capiocl.config_path", &workflow_file, "");
        config.capio_cl_config.getParameter("capiocl.monitor.mcast.enabled", &mcast_monitor,
                                            "false");
        config.capio_cl_config.getParameter("capiocl.monitor.filesystem.enabled", &fs_monitor,
                                            "false");
        CALF_PRINT_COLOR(
            CALF_CLI_LEVEL_WARNING,
            "  backend=%s, CAPIO-CL workflow file=%s, CAPIO-CL monitors: mcast=%s "
            "filesystem=%s",
            config.backend_name.c_str(), workflow_file.empty() ? "<none>" : workflow_file.c_str(),
            mcast_monitor == "true" ? "on" : "off", fs_monitor == "true" ? "on" : "off");
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "  run 'capio_server --print-config' to see every value, "
                         "'capio_server --genconf' to create a configuration file");
    }
    check_application_environment(config);

    if (config.continue_on_error) {
#ifdef CAPIO_LOG
        continue_on_error = true;
        for (const auto line : CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING) {
            CALF_PRINT("%s", line);
        }
#else
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "capio.continue_on_error is enabled, but logging is not compiled in. "
                         "Option is ignored.");
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
