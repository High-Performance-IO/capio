#include "utils/runtime_configuration.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "capiocl/parser.h"
#include "common/constants.hpp"
#include "utils/common.hpp"

#include <args.hxx>

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::array CAPIO_DIR_FORBIDDEN_PATHS = {std::string_view{"/proc/"},
                                                  std::string_view{"/sys/"}};

/// @brief Read a numeric environment variable, falling back to @p default_value when unset
long long env_number(const char *name, long long default_value) {
    const char *value = std::getenv(name);
    return value == nullptr ? default_value : std::strtoll(value, nullptr, 10);
}

std::filesystem::path resolve_capio_dir() {
    const char *value = std::getenv("CAPIO_DIR");
    if (value == nullptr) {
        throw std::runtime_error("CAPIO_DIR not provided");
    }
    std::error_code error;
    auto capio_dir = std::filesystem::canonical(value, error);
    if (error) {
        throw std::runtime_error(std::string{"CAPIO_DIR set, but directory "} + value +
                                 " does not exist on filesystem");
    }
    for (const auto &forbidden_path : CAPIO_DIR_FORBIDDEN_PATHS) {
        if (capio_dir.native().rfind(forbidden_path, 0) == 0) {
            throw std::runtime_error("CAPIO_DIR " + capio_dir.string() + " is in forbidden path");
        }
    }
    return capio_dir;
}

std::optional<RuntimeConfiguration> &global_configuration() {
    static std::optional<RuntimeConfiguration> configuration;
    return configuration;
}

} // namespace

RuntimeConfiguration::RuntimeConfiguration(int argc, char **argv) {
    args::ArgumentParser parser(CAPIO_SERVER_ARG_PARSER_PRE, CAPIO_SERVER_ARG_PARSER_EPILOGUE);
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> resolve_prefix(arguments, "resolve-prefix",
                                                CAPIO_SERVER_ARG_PARSER_RESOLVE_PREFIX_OPT_HELP,
                                                {'r', "resolve-prefix"});

    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'c', "config"});
    args::Flag noConfigFile(arguments, "no-config",
                            CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP, {"no-config"});
    args::ValueFlag<std::string> backend_flag(
        arguments, "backend", CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP, {'b', "backend"});
    args::ValueFlag<std::string> discovery_flag(
        arguments, "discovery", CAPIO_SERVER_ARG_PARSER_DISCOVERY_HELP, {"discovery"});
    args::ValueFlag<std::string> mcast_addr_flag(
        arguments, "address", CAPIO_SERVER_ARG_PARSER_MCAST_ADDR_HELP, {"mcast-addr"});
    args::ValueFlag<unsigned int> mcast_port_flag(
        arguments, "port", CAPIO_SERVER_ARG_PARSER_MCAST_PORT_HELP, {"mcast-port"});
    args::ValueFlag<std::string> token_directory_flag(
        arguments, "directory", CAPIO_SERVER_ARG_PARSER_TOKEN_DIRECTORY_HELP, {"token-directory"});
    args::ValueFlag<std::string> backend_options_flag(
        arguments, "options", CAPIO_SERVER_ARG_PARSER_BACKEND_OPTIONS_HELP, {"backend-options"});

    args::Flag continueOnErrorFlag(arguments, "continue-on-error",
                                   CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP,
                                   {"continue-on-error"});
    args::Flag mem_only_flag(arguments, "mem-only",
                             CAPIO_SERVER_ARG_PARSER_STORE_ALL_IN_MEMORY_OPT_HELP, {"mem-only"});

    const auto usage_error = [&parser](const std::string &message) {
        std::ostringstream usage;
        usage << message << "\n" << parser;
        return std::runtime_error(usage.str());
    };

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << parser;
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        throw usage_error(e.what());
    } catch (args::ValidationError &e) {
        throw usage_error(e.what());
    }

    discovery_type = discovery_flag ? args::get(discovery_flag) : CAPIO_MCAST_PROTO_FLAG;
    if (discovery_type != CAPIO_MCAST_PROTO_FLAG && discovery_type != CAPIO_FS_PROTO_FLAG) {
        throw std::runtime_error("Invalid discovery interface: " + discovery_type +
                                 ". Expected mcast or fs.");
    }
    mcast_address = mcast_addr_flag ? args::get(mcast_addr_flag) : CAPIO_MCAST_ADV_DEFAULT_ADDR;
    mcast_port    = mcast_port_flag ? args::get(mcast_port_flag) : CAPIO_MCAST_ADV_DEFAULT_PORT;
    token_directory =
        token_directory_flag ? args::get(token_directory_flag) : CAPIO_FS_DEFAULT_TOKEN_DIRECTORY;

    continue_on_error   = continueOnErrorFlag;
    store_all_in_memory = mem_only_flag;

    if (config) {
        capio_cl_config_path = args::get(config);
        if (capio_cl_config_path == "dynamic") {
            capio_cl_dynamic_config = true;
        } else if (resolve_prefix) {
            capio_cl_resolve_path = args::get(resolve_prefix);
        }
    } else if (!noConfigFile) {
        throw std::runtime_error(
            "Error: no config file provided. To skip config file use --no-config option!");
    }

    backend_name       = backend_flag ? args::get(backend_flag) : "";
    mtcl_protocol      = CAPIO_MTCL_DEFAULT_PROTOCOL;
    mtcl_port          = CAPIO_MTCL_DEFAULT_PORT;
    mtcl_poll_interval = CAPIO_MTCL_DEFAULT_POLL_INTERVAL;
    if (const auto options = backend_options_flag ? args::get(backend_options_flag) : "";
        backend_name == "mtcl" && !options.empty()) {
        const auto colon = options.find(':');
        const auto at    = options.find('@', colon + 1);
        if (colon == std::string::npos || at == std::string::npos || colon == 0 ||
            at == colon + 1 || at + 1 == options.size() ||
            options.find(':', colon + 1) != std::string::npos ||
            options.find('@', at + 1) != std::string::npos) {
            throw std::runtime_error("Invalid MTCL backend options '" + options +
                                     "'. Expected PROTO:PORT@POLL_INTERVAL_US.");
        }
        mtcl_protocol = options.substr(0, colon);
        try {
            size_t parsed = 0;
            mtcl_port     = std::stoi(options.substr(colon + 1, at - colon - 1), &parsed);
            if (parsed != at - colon - 1 || mtcl_port <= 0 || mtcl_port > 65535) {
                throw std::invalid_argument("invalid port");
            }
            mtcl_poll_interval = std::stoi(options.substr(at + 1), &parsed);
            if (parsed != options.size() - at - 1 || mtcl_poll_interval <= 0) {
                throw std::invalid_argument("invalid poll interval");
            }
        } catch (const std::exception &) {
            throw std::runtime_error("Invalid MTCL port or poll interval in backend options '" +
                                     options + "'. Expected positive integers.");
        }
    }

    capio_dir          = resolve_capio_dir();
    cache_lines        = env_number("CAPIO_CACHE_LINES", CAPIO_CACHE_LINES_DEFAULT);
    cache_line_size    = env_number("CAPIO_CACHE_LINE_SIZE", CAPIO_CACHE_LINE_SIZE_DEFAULT);
    file_initial_size  = env_number("CAPIO_FILE_INIT_SIZE", CAPIO_DEFAULT_FILE_INITIAL_SIZE);
    prefetch_data_size = env_number("CAPIO_PREFETCH_DATA_SIZE", 0);
}

const std::filesystem::path &RuntimeConfiguration::getCapioDir() const { return capio_dir; }

bool RuntimeConfiguration::getContinueOnError() const { return continue_on_error; }

long RuntimeConfiguration::getCacheLines() const { return cache_lines; }

long RuntimeConfiguration::getCacheLineSize() const { return cache_line_size; }

off64_t RuntimeConfiguration::getFileInitialSize() const { return file_initial_size; }

off64_t RuntimeConfiguration::getPrefetchDataSize() const { return prefetch_data_size; }

const std::string &RuntimeConfiguration::getDiscoveryType() const { return discovery_type; }

const std::string &RuntimeConfiguration::getMulticastDiscoveryAddress() const {
    return mcast_address;
}

unsigned int RuntimeConfiguration::getMulticastDiscoveryPort() const { return mcast_port; }

const std::string &RuntimeConfiguration::getFsDiscoveryTokenDirectory() const {
    return token_directory;
}

const std::string &RuntimeConfiguration::getBackendName() const { return backend_name; }

const std::string &RuntimeConfiguration::getMtclProtocol() const { return mtcl_protocol; }

int RuntimeConfiguration::getMtclPort() const { return mtcl_port; }

int RuntimeConfiguration::getMtclPollInterval() const { return mtcl_poll_interval; }

capiocl::engine::Engine *RuntimeConfiguration::createCapioClEngine() const {
    capiocl::engine::Engine *engine;
    if (capio_cl_dynamic_config) {
        engine = new capiocl::engine::Engine();
        engine->startApiServer();
    } else if (!capio_cl_config_path.empty()) {
        engine = capiocl::parser::Parser::parse(capio_cl_config_path, capio_cl_resolve_path,
                                                store_all_in_memory);
    } else {
        const char *workflow_name = std::getenv("CAPIO_WORKFLOW_NAME");
        engine                    = new capiocl::engine::Engine();
        engine->setWorkflowName(workflow_name != nullptr ? workflow_name
                                                         : CAPIO_DEFAULT_WORKFLOW_NAME);
    }
    if (store_all_in_memory) {
        engine->setAllStoreInMemory();
    }
    return engine;
}

void RuntimeConfiguration::set(RuntimeConfiguration configuration) {
    global_configuration() = std::move(configuration);
}

const RuntimeConfiguration &RuntimeConfiguration::get() {
    const auto &configuration = global_configuration();
    if (!configuration) {
        throw std::logic_error("RuntimeConfiguration::get() called before initialization");
    }
    return *configuration;
}

void RuntimeConfiguration::initialize(int argc, char **argv) {
    try {
        set(RuntimeConfiguration(argc, argv));
    } catch (const std::exception &error) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "%s", error.what());
        exit(EXIT_FAILURE);
    }
    const auto &config = get();

    if (config.getContinueOnError()) {
#ifdef CAPIO_LOG
        ::continue_on_error = true;
        for (const auto line : CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING) {
            CALF_PRINT("%s", line);
        }

#else
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "--continue-on-error flag given, but logger is not compiled into "
                         "CAPIO. Flag is ignored.");
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

    if (config.capio_cl_dynamic_config) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO,
                         "Starting CAPIO-CL engine with dynamic configuration");
    } else if (config.capio_cl_config_path.empty()) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "skipping config file parsing.");
    }

    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "CAPIO_DIR=%s", config.getCapioDir().c_str());
}
