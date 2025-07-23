#ifndef PARSER_HPP
#define PARSER_HPP

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

    args::ValueFlag<std::string> backend(
        arguments, "backend", CAPIO_SERVER_ARG_PARSER_BACKEND_OPT_HELP, {'b', "backend"});

    args::ValueFlag<int> backend_port(arguments, "port",
                                      CAPIO_SERVER_ARG_PARSER_BACKEND_PORT_OPT_HELP, {'p', "port"});

    args::Flag continueOnErrorFlag(arguments, "continue-on-error",
                                   CAPIO_SERVER_ARG_PARSER_MEM_STORAGE_ONLY_HELP,
                                   {"continue-on-error"});

    args::Flag memStorageOnly(arguments, "mem-storage-only",
                              CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP, {"mem-only"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << CAPIO_SERVER_ARG_PARSER_PRE_COMMAND << parser;
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        START_LOG(gettid(), "call()");
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        ERR_EXIT("%s", e.what());
    } catch (args::ValidationError &e) {
        START_LOG(gettid(), "call()");
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        ERR_EXIT("%s", e.what());
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

    if (memStorageOnly) {
        StoreOnlyInMemory = true;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
                  << "All files will be stored in memory whenever possible." << std::endl;
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
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
              << "started logging to logfile " << logname << std::endl;
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
        START_LOG(gettid(), "call()");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                  << "Error: no config file provided. To skip config file use --no-config option!"
                  << std::endl;
        ERR_EXIT("no config file provided, and  --no-config not provided");
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

    if (backend) {
        std::string backend_name = args::get(backend);
        std::transform(backend_name.begin(), backend_name.end(), backend_name.begin(), ::toupper);

        int port = DEFAULT_CAPIO_BACKEND_PORT;
        if (backend_port) {
            port = args::get(backend_port);
        }

        capio_communication_service = new CapioCommunicationService(backend_name, port);

    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
                  << "Selected backend is File System" << std::endl;
        capio_backend = new NoBackend();
    }

    if (config) {
        return args::get(config);
    }
    return "";
}

#endif // PARSER_HPP
