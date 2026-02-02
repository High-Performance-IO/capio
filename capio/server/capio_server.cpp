#include <algorithm>
#include <args.hxx>
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <semaphore.h>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils/capiocl_adapter.hpp"

std::string workflow_name;

#include "client-manager/client_manager.hpp"
#include "common/env.hpp"
#include "common/logger.hpp"
#include "common/requests.hpp"
#include "common/semaphore.hpp"
#include "utils/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/env.hpp"
#include "utils/types.hpp"

ClientManager *client_manager;
StorageManager *storage_manager;

int n_servers;
// name of the node
char *node_name;

// application name -> set of files already sent
CSFilesSentMap_t files_sent;

CSClientsRemotePendingNFilesMap_t clients_remote_pending_nfiles;

std::mutex nfiles_mutex;

#include "handlers.hpp"
#include "utils/location.hpp"
#include "utils/signals.hpp"

#include "remote/listener.hpp"

/**
 * The capio_cl_engine is declared here to ensure that other components of the CAPIO server
 * can only access it through a const reference. This prevents any modifications to the engine
 * outside of those permitted by the capiocl::Engine class itself.
 */
capiocl::Engine *capio_cl_engine;
const capiocl::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_ACCESS]              = access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]               = clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]               = close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]              = create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE]    = create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]                 = dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]               = fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]            = getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]          = getdents_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = handshake_anonymous_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]               = mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]                = open_handler;
    _request_handlers[CAPIO_REQUEST_READ]                = read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]              = rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]               = rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]                = lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]           = seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]            = seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]           = seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]                = stat_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]              = unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;

    return _request_handlers;
}

[[noreturn]] void capio_server(Semaphore &internal_server_sem) {
    static const std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers =
        build_request_handlers_table();

    START_LOG(gettid(), "call()");

    MPI_Comm_size(MPI_COMM_WORLD, &n_servers);
    setup_signal_handlers();
    backend->handshake_servers();

    storage_manager->addDirectory(getpid(), get_capio_dir());

    internal_server_sem.unlock();

    auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
    while (true) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        int code = client_manager->readNextRequest(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                      << std::endl;

            ERR_EXIT("Error: received invalid request code");
        }
        request_handlers[code](str.get());
        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }
}

int parseCLI(int argc, char **argv) {
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
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "started logging to logfile " << logname
              << std::endl;
#endif
    bool store_all_in_memory = false;

    if (mem_only_flag) {
        store_all_in_memory = args::get(mem_only_flag);
    }

    if (config) {
        std::string token                  = args::get(config);
        std::filesystem::path resolve_path = "";

        if (resolve_prefix) {
            resolve_path = args::get(resolve_prefix);
        }

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "parsing config file: " << token
                  << std::endl;

        std::tie(workflow_name, capio_cl_engine) =
            capiocl::Parser::parse(token, resolve_path, store_all_in_memory);
    } else if (noConfigFile) {
        workflow_name   = std::string_view(get_capio_workflow_name());
        capio_cl_engine = new capiocl::Engine();
        if (store_all_in_memory) {
            capio_cl_engine->setAllStoreInMemory();
        }

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "skipping config file parsing."
                  << std::endl
                  << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "Obtained from environment variable current workflow name: "
                  << workflow_name.data() << std::endl;
    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Error: no config file provided. To skip config file use --no-config option!"
                  << std::endl;
#ifdef CAPIO_LOG
        log->log("no config file provided, and  --no-config not provided");
#endif
        exit(EXIT_FAILURE);
    }

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "CAPIO_DIR=" << get_capio_dir().c_str()
              << std::endl;

    capio_cl_engine->print();

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "LOG_LEVEL set to: " << CAPIO_LOG_LEVEL
              << std::endl;
    std::cout << CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING;
    log->log("LOG_LEVEL set to: %d", CAPIO_LOG_LEVEL);
    delete log;
#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE << std::endl;
    }
#endif

    // Backend selection phase
    std::string backend_name_str;
    if (backend_flag) {
        backend_name_str = args::get(backend_flag);
    }
    backend = select_backend(backend_name_str, argc, argv);

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "server initialization completed!" << std::endl
              << std::flush;
    return 0;
}

int main(int argc, char **argv) {

    Semaphore internal_server_sem(0);

    std::cout << CAPIO_LOG_SERVER_BANNER;

    parseCLI(argc, argv);

    START_LOG(gettid(), "call()");

    open_files_location();

    shm_canary      = new CapioShmCanary(workflow_name);
    storage_manager = new StorageManager();
    client_manager  = new ClientManager();

    std::thread server_thread(capio_server, std::ref(internal_server_sem));
    LOG("capio_server thread started");
    std::thread remote_listener_thread(capio_remote_listener, std::ref(internal_server_sem));
    LOG("capio_remote_listener thread started.");
    server_thread.join();
    remote_listener_thread.join();

    delete backend;
    return 0;
}