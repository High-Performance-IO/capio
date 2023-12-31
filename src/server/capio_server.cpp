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

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "utils/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/env.hpp"
#include "utils/json.hpp"
#include "utils/metadata.hpp"
#include "utils/requests.hpp"

using namespace simdjson;

int n_servers;
// name of the node
char *node_name;

/*
 * For multithreading:
 * tid -> pid*/
CSPidsMap_T pids;

// tid -> application name
CSAppsMap_t apps;

// application name -> set of files already sent
CSFilesSentMap_t files_sent;

// tid -> (client_to_server_data_buf, server_to_client_data_buf)
CSDataBufferMap_t data_buffers;

/*
 * pid -> pathname -> bool
 * Different threads with the same pid are treated as a single writer
 */
CSWritersMap_t writers;

// node -> rank
CSNodesHelperRankMap_t nodes_helper_rank;

// rank -> node
CSRankToNodeMap_t rank_to_node;

/*
 * It contains all the reads requested by local processes to read files that
 * are in the local node for which the data is not yet available. path -> [(tid,
 * fd, numbytes, is_getdents), ...]
 */
CSPendingReadsMap_t pending_reads;

// it contains the file saved on disk
CSOnDiskMap_t on_disk;

CSClientsRemotePendingNFilesMap_t clients_remote_pending_nfiles;

sem_t internal_server_sem;

sem_t clients_remote_pending_nfiles_sem;

#include "handlers.hpp"
#include "utils/location.hpp"
#include "utils/signals.hpp"

#include "comms/remote_listener.hpp"

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
    _request_handlers[CAPIO_REQUEST_GETDENTS64]          = getdents64_handler;
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
    _request_handlers[CAPIO_REQUEST_STAT_REPLY]          = stat_reply_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]              = unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;

    return _request_handlers;
}

void capio_server(int rank) {
    static const std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers =
        build_request_handlers_table();

    START_LOG(gettid(), "call(rank=%d)", rank);

    MPI_Comm_size(MPI_COMM_WORLD, &n_servers);
    setup_signal_handlers();
    backend->handshake_servers(rank);
    open_files_location(rank);
    pid_t pid                              = getpid();
    const std::filesystem::path &capio_dir = get_capio_dir();
    create_dir(pid, capio_dir.c_str(), rank,
               true); // TODO: can be a problem if a process execute readdir
    // on capio_dir

    init_server();

    if (sem_post(&internal_server_sem) == -1) {
        ERR_EXIT("sem_post internal_server_sem in capio_server");
    }

    auto str = std::unique_ptr<char[]>(new char[CAPIO_REQUEST_MAX_SIZE]);
    while (true) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        int code = read_next_request(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            ERR_EXIT("Received an invalid request code %d", code);
        }
        request_handlers[code](str.get(), rank);
        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }
}

int parseCLI(int argc, char **argv, int rank) {
    Logger *log;

    args::ArgumentParser parser(CAPIO_SERVER_ARG_PARSER_PRE, CAPIO_SERVER_ARG_PARSER_EPILOGUE);
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> logfile_src(arguments, "filename",
                                             CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP, {'l', "log"});
    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'c', "config"});
    args::Flag noConfigFile(arguments, "no-config",
                            CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP, {"no-config"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << CAPIO_SERVER_ARG_PARSER_PRE_COMMAND << parser;
        MPI_Finalize();
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        MPI_Finalize();
        exit(EXIT_FAILURE);
    } catch (args::ValidationError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }
    if (logfile_src) {
        // log file was given
        std::string token = args::get(logfile_src);
        if (token.find(".log") != std::string::npos) {
            token.erase(token.length() - 4); // delete .log if for some reason
            // is given as parameter
        }

        std::string filename = token + "_" + std::to_string(rank) + ".log";
        logfile.open(filename, std::ofstream::out);
        log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "started logging to: " << filename
                  << std::endl;
    } else {
        // log file not given. starting with default name
        const std::string logname(CAPIO_LOG_SERVER_DEFAULT_FILE_NAME + std::to_string(rank) +
                                  ".log");
        logfile.open(logname, std::ofstream::out);
        log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "started logging to default logfile "
                  << logname << std::endl;
    }

    if (noConfigFile) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "skipping config file parsing" << std::endl;
    } else {
        if (config) {
            std::string token                      = args::get(config);
            const std::filesystem::path &capio_dir = get_capio_dir();
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "parsing config file: " << token
                      << std::endl;
            parse_conf_file(token, capio_dir);
        } else {
            std::cout
                << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                << "Error: no config file provided. To skip config file use --no-config option!"
                << std::endl;
#ifdef CAPIOLOG
            log->log("no config file provided, and  --no-config not provided");
#endif
            exit(EXIT_FAILURE);
        }
    }

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "CAPIO_DIR=" << get_capio_dir().c_str()
              << std::endl;

    delete log;

#ifdef CAPIOLOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "LOG_LEVEL set to: " << CAPIO_LOG_LEVEL
              << std::endl;
    std::cout << CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING;

#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE << std::endl;
    }
#endif

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "server initialization completed!"
              << std::flush;
    return 0;
}

int main(int argc, char **argv) {
    int rank = 0, provided = 0;

    std::cout << CAPIO_LOG_SERVER_BANNER;
    backend = new MPI_backend();

    parseCLI(argc, argv, rank);

    START_LOG(gettid(), "call()");

    backend->initialize(argc, argv, &rank, &provided);

    int res = sem_init(&internal_server_sem, 0, 0);
    if (res != 0) {
        ERR_EXIT("sem_init internal_server_sem failed with status %d", res);
    }
    if (sem_init(&(backend->remote_read_sem), 0, 1) == -1) {
        ERR_EXIT("sem_init remote_read_sem in main");
    }
    if (sem_init(&clients_remote_pending_nfiles_sem, 0, 1) == -1) {
        ERR_EXIT("sem_init clients_remote_pending_nfiles_sem in main");
    }
    std::thread server_thread(capio_server, rank);
    std::thread helper_thread(capio_remote_listener);
    server_thread.join();
    helper_thread.join();

    backend->destroy(new std::vector<sem_t *>{&internal_server_sem, &(backend->remote_read_sem)});

    return 0;
}