#include "utils/cli_parser.hpp"

#include "common/constants.hpp"
#include "common/logger.hpp"
#include "utils/common.hpp"

#include <args.hxx>

CapioParsedConfig parseCLI(int argc, char **argv) {
    CapioParsedConfig capio_config;
    Logger *log;

    args::ArgumentParser parser(CAPIO_SERVER_ARG_PARSER_PRE, CAPIO_SERVER_ARG_PARSER_EPILOGUE);
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> logfile_src(arguments, "filename",
                                             CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP, {'l', "log"});
    args::ValueFlag<std::string> logfile_folder(
        arguments, "filename", CAPIO_SERVER_ARG_PARSER_LOGILE_DIR_OPT_HELP, {'d', "log-dir"});
    args::ValueFlag<std::string> resolve_prefix(arguments, "resolve-prefix",
                                                CAPIO_SERVER_ARG_PARSER_RESOLVE_PREFIX_OPT_HELP,
                                                {'r', "resolve-prefix"});

    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'c', "config"});
    args::Flag noConfigFile(arguments, "no-config",
                            CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP, {"no-config"});
    args::ValueFlag<std::string> backend_flag(
        arguments, "backend", CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP, {'b', "backend"});

    args::Flag continueOnErrorFlag(arguments, "continue-on-error",
                                   CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP,
                                   {"continue-on-error"});
    args::Flag mem_only_flag(arguments, "mem-only",
                             CAPIO_SERVER_ARG_PARSER_STORE_ALL_IN_MEMORY_OPT_HELP, {"mem-only"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << parser;
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        exit(EXIT_FAILURE);
    } catch (args::ValidationError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        exit(EXIT_FAILURE);
    }

    if (continueOnErrorFlag) {
#ifdef CAPIO_LOG
        continue_on_error = true;
        for (const auto line : CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, "parseCLI", line);
        }

#else
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       "--continue-on-error flag given, but logger is not compiled into CAPIO. "
                       "Flag is ignored.");
#endif
    }

    if (logfile_folder) {
#ifdef CAPIO_LOG
        log_master_dir_name = args::get(logfile_folder);
#else
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       "Capio logfile folder, but logging capabilities not compiled into capio!");
#endif
    }

    if (logfile_src) {
#ifdef CAPIO_LOG
        // log file was given
        std::string token = args::get(logfile_src);
        if (token.find(".log") != std::string::npos) {
            token.erase(token.length() - 4); // delete .log if for some reason
            // is given as parameter
        }
        logfile_prefix = token;
#else
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       "Capio logfile provided, but logging capabilities not compiled into capio!");
#endif
    }
#ifdef CAPIO_LOG
    auto logname = open_server_logfile();
    log          = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parseCLI",
                   "started logging to logfile " + logname.string());
#endif

    if (mem_only_flag) {
        capio_config.store_all_in_memory = args::get(mem_only_flag);
    }

    if (config) {

        capio_config.capio_cl_config_path = args::get(config);

        if (std::string token = args::get(config); token == "dynamic") {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parseCLI",
                           "Starting CAPIO-CL engine with dynamic configuration");
            capio_config.capio_cl_dynamic_config = true;

        } else {
            std::filesystem::path resolve_path = "";

            if (resolve_prefix) {
                capio_config.capio_cl_resolve_path = args::get(resolve_prefix);
            }
        }

    } else if (noConfigFile) {

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       "skipping config file parsing.");
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       "Obtained from environment variable current workflow name: " +
                           get_capio_workflow_name());
    } else {
        server_println(
            CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, "parseCLI",
            "Error: no config file provided. To skip config file use --no-config option!");
#ifdef CAPIO_LOG
        log->log("no config file provided, and  --no-config not provided");
#endif
        exit(EXIT_FAILURE);
    }

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parseCLI",
                   "CAPIO_DIR=" + get_capio_dir().string());

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parseCLI",
                   "LOG_LEVEL set to: " + std::to_string(CAPIO_LOG_LEVEL));
    for (const auto &msg : CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Logger", msg);
    }

    log->log("LOG_LEVEL set to: %d", CAPIO_LOG_LEVEL);
    delete log;
#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI",
                       CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE);
    }
#endif

    // Backend selection phase
    if (backend_flag) {
        capio_config.backend_name = args::get(backend_flag);
    }

    return capio_config;
}