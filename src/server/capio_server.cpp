#include <dirent.h>
#include <fcntl.h>
#include <semaphore.h>
#include <singleheader/simdjson.h>
#include <unistd.h>

#include <algorithm>
#include <args.hxx>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::string workflow_name;
char node_name[HOST_NAME_MAX];

#include "utils/types.hpp"

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"

#include "client-manager/request_handler_engine.hpp"
#include "ctl-module/capio_ctl.hpp"
#include "utils/signals.hpp"

#include "file-manager/file_manager.hpp"
#include "storage-service/capio_storage_service.hpp"

std::string parseCLI(int argc, char **argv) {
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
    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'c', "config"});
    args::Flag noConfigFile(arguments, "no-config",
                            CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP, {"no-config"});

    args::Flag continueOnErrorFlag(arguments, "continue-on-error",
                                   CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP,
                                   {"continue-on-error"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << CAPIO_SERVER_ARG_PARSER_PRE_COMMAND << parser;
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
        std::cout << CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING << std::endl;
#else
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "--continue-on-error flag given, but logger is not compiled into CAPIO. Flag "
                     "is ignored."
                  << std::endl;
#endif
    }

    if (logfile_folder) {
#ifdef CAPIO_LOG
        log_master_dir_name = args::get(logfile_folder);
#else
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "Capio logfile folder, but logging capabilities not compiled into capio!"
                  << std::endl;
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
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "Capio logfile provided, but logging capabilities not compiled into capio!"
                  << std::endl;
#endif
    }
#ifdef CAPIO_LOG
    auto logname = open_server_logfile();
    log          = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "started logging to logfile " << logname
              << std::endl;
#endif

    if (config) {
        std::string token = args::get(config);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
                  << "parsing config file: " << token << std::endl;
        // TODO: pass config file path
    } else if (noConfigFile) {
        workflow_name = std::string_view(get_capio_workflow_name());
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << "skipping config file parsing." << std::endl
                  << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << "Obtained from environment variable current workflow name: "
                  << workflow_name.data() << std::endl;

    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                  << "Error: no config file provided. To skip config file use --no-config option!"
                  << std::endl;
#ifdef CAPIO_LOG
        log->log("no config file provided, and  --no-config not provided");
#endif
        exit(EXIT_FAILURE);
    }

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "CAPIO_DIR=" << get_capio_dir().c_str() << std::endl;

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "LOG_LEVEL set to: " << CAPIO_LOG_LEVEL << std::endl;
    std::cout << CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING;
    log->log("LOG_LEVEL set to: %d", CAPIO_LOG_LEVEL);
    delete log;
#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE << std::endl;
    }
#endif

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "server initialization completed!" << std::endl
              << std::flush;

    if (config) {
        return std::string(args::get(config).data());
    }
    return "";
}

int main(int argc, char **argv) {

    std::cout << CAPIO_LOG_SERVER_BANNER;
    gethostname(node_name, HOST_NAME_MAX);
    const std::string config_path = parseCLI(argc, argv);

    START_LOG(gettid(), "call()");
    setup_signal_handlers();

    capio_cl_engine         = JsonParser::parse(config_path);
    shm_canary              = new CapioShmCanary(workflow_name);
    file_manager            = new CapioFileManager();
    fs_monitor              = new FileSystemMonitor();
    storage_service         = new CapioStorageService();
    ctl_module              = new CapioCTLModule();
    request_handlers_engine = new RequestHandlerEngine();

    capio_cl_engine->print();
    request_handlers_engine->start();

    return 0;
}