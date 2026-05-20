#include "utils/cli_parser.hpp"

#include "captura/StlLogger.h"
#include "common/constants.hpp"
#include "utils/common.hpp"
#include "utils/server_println.hpp"

#include <args.hxx>

CapioParsedConfig parseCLI(int argc, char **argv) {
    CapioParsedConfig capio_config;

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
            server_println(line, get_capio_workflow_name(), CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           __func__);
        }

#else
        server_println("--continue-on-error flag given, but logger is not compiled into CAPIO. "
                       "Flag is ignored.",
                       get_capio_workflow_name(), CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "parseCLI");
#endif
    }

#ifdef CAPIO_LOG
    auto log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
    server_println("started logging to logfile " + log->getLogFileName(), get_capio_workflow_name(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parseCLI");
#endif

    if (mem_only_flag) {
        capio_config.store_all_in_memory = args::get(mem_only_flag);
    }

    if (config) {

        capio_config.capio_cl_config_path = args::get(config);

        if (std::string token = args::get(config); token == "dynamic") {
            server_println("Starting CAPIO-CL engine with dynamic configuration",
                           get_capio_workflow_name(), CAPIO_LOG_SERVER_CLI_LEVEL_INFO, __func__);
            capio_config.capio_cl_dynamic_config = true;

        } else {
            std::filesystem::path resolve_path = "";

            if (resolve_prefix) {
                capio_config.capio_cl_resolve_path = args::get(resolve_prefix);
            }
        }

    } else if (noConfigFile) {

        server_println("skipping config file parsing.", get_capio_workflow_name(),
                       CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, __func__);
        server_println("Obtained from environment variable current workflow name: " +
                           get_capio_workflow_name(),
                       get_capio_workflow_name(), CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, __func__);
    } else {
        server_println(
            "Error: no config file provided. To skip config file use --no-config option!",
            get_capio_workflow_name(), CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, __func__);
#ifdef CAPIO_LOG
        log->log("no config file provided, and  --no-config not provided");
#endif
        exit(EXIT_FAILURE);
    }

    server_println("CAPIO_DIR=" + get_capio_dir().string(), get_capio_workflow_name(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_INFO, __func__);

    // Backend selection phase
    if (backend_flag) {
        capio_config.backend_name = args::get(backend_flag);
    }

    return capio_config;
}