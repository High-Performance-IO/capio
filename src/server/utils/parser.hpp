#ifndef PARSER_HPP
#define PARSER_HPP

std::string parseCLI(int argc, char **argv, char *resolve_prefix) {
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

    args::ValueFlag<std::string> capio_cl_resolve_path(
        arguments, "capio-cl-relative-to", CAPIO_SERVER_ARG_PARSER_CONFIG_RESOLVE_RELATIVE_TO_HELP,
        {"resolve-capiocl-to"});

    args::ValueFlag<std::string> controlPlaneBackend(
        arguments, "backend", CAPIO_SERVER_ARG_PARSER_CONFIG_CONTROL_PLANE_BACKEND,
        {"control-backend"});

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

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "--continue-on-error flag given, but logger is not compiled into CAPIO. "
                       "Flag is ignored.");
#endif
    }

    if (memStorageOnly) {
        capio_global_configuration->StoreOnlyInMemory = true;
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                       "All files will be stored in memory whenever possible.");
    }

    if (logfile_folder) {
#ifdef CAPIO_LOG
        log_master_dir_name = args::get(logfile_folder);
#else
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
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
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Capio logfile provided, but logging capabilities not compiled into capio!");
#endif
    }
#ifdef CAPIO_LOG
    auto logname = open_server_logfile();
    log          = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "started logging to logfile " + logname.string());
#endif

    if (config) {
        std::string token = args::get(config);
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "parsing config file: " + token);
        // TODO: pass config file path
    } else if (noConfigFile) {
        capio_global_configuration->workflow_name = std::string_view(get_capio_workflow_name());
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "skipping config file parsing.");
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Obtained from environment variable current workflow name: " +
                           capio_global_configuration->workflow_name);

    } else {
        START_LOG(gettid(), "call()");
        server_println(
            CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
            "Error: no config file provided. To skip config file use --no-config option!");
        ERR_EXIT("no config file provided, and  --no-config not provided");
    }

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "LOG_LEVEL set to: " + std::to_string(CAPIO_LOG_LEVEL));
    std::cout << CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING;
    log->log("LOG_LEVEL set to: %d", CAPIO_LOG_LEVEL);
    delete log;
#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE);
    }
#endif

    if (backend) {
        std::string backend_name = args::get(backend);
        std::transform(backend_name.begin(), backend_name.end(), backend_name.begin(), ::toupper);

        int port = DEFAULT_CAPIO_BACKEND_PORT;
        if (backend_port) {
            port = args::get(backend_port);
        }

        std::string control_backend_name = "multicast";
        if (controlPlaneBackend) {
            auto tmp = args::get(controlPlaneBackend);
            if (tmp != "multicast" && tmp != "fs") {
                server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                               "Unknown control plane backend " + tmp);
            } else {
                control_backend_name = tmp;
            }
        }

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                       "Using control plane backend: " + control_backend_name);

        capio_communication_service =
            new CapioCommunicationService(backend_name, port, control_backend_name);

    } else {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Selected backend is File System");
        capio_backend = new NoBackend();
    }

    if (capio_cl_resolve_path) {
        auto path = args::get(capio_cl_resolve_path);
        memcpy(resolve_prefix, path.c_str(), PATH_MAX);
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "CAPIO-CL relative file prefix: " + path);
    } else {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "No CAPIO-CL resolve file prefix provided");
    }

    if (config) {
        return args::get(config);
    }
    return "";
}

#endif // PARSER_HPP
