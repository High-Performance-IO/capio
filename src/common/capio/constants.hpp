#ifndef CAPIO_COMMON_CONSTANTS_HPP
#define CAPIO_COMMON_CONSTANTS_HPP

#include <array>
#include <climits>
#include <string_view>

#include <sys/types.h>

// 64bit unsigned long int type for file offsets
typedef unsigned long long int capio_off64_t;

// CAPIO files constants
constexpr size_t CAPIO_DEFAULT_DIR_INITIAL_SIZE   = 1024L * 1024 * 1024;
constexpr off64_t CAPIO_DEFAULT_FILE_INITIAL_SIZE = 1024L * 1024 * 1024 * 4;
constexpr std::array CAPIO_DIR_FORBIDDEN_PATHS    = {std::string_view{"/proc/"},
                                                     std::string_view{"/sys/"}};

// CAPIO default values for shared memory
constexpr char CAPIO_DEFAULT_WORKFLOW_NAME[] = "CAPIO";
constexpr char CAPIO_DEFAULT_APP_NAME[]      = "default_app";
constexpr char CAPIO_SHM_CANARY_ERROR[] =
    "FATAL ERROR:  Shared memories for workflow %s already "
    "exists. One of two (or both) reasons are to blame: \n             "
    "either a previous run of CAPIO terminated without "
    "cleaning up memory, or another instance of CAPIO\n             "
    "is already running. Clean shared memory and then retry";

// CAPIO communication constants
constexpr int CAPIO_REQ_BUFF_CNT                     = 512; // Max number of elements inside buffers
constexpr int CAPIO_CACHE_LINES_DEFAULT              = 10;
constexpr int CAPIO_CACHE_LINE_SIZE_DEFAULT          = 4096;
constexpr size_t CAPIO_REQ_MAX_SIZE                  = 256 * sizeof(char);
constexpr char CAPIO_SERVER_CLI_LOG_SERVER[]         = "[ \033[1;32m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_WARNING[] = "[ \033[1;33m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_ERROR[]   = "[ \033[1;31m SERVER \033[0m ] ";
constexpr char LOG_CAPIO_START_REQUEST[]             = "\n+++++++++++ SYSCALL %s (%d) +++++++++++";
constexpr char LOG_CAPIO_END_REQUEST[]               = "----------- END SYSCALL ----------\n";
constexpr char CAPIO_SERVER_LOG_START_REQUEST_MSG[] = "\n+++++++++++++++++REQUEST+++++++++++++++++";
constexpr char CAPIO_SERVER_LOG_END_REQUEST_MSG[]   = "~~~~~~~~~~~~~~~END REQUEST~~~~~~~~~~~~~~~";
constexpr int CAPIO_LOG_MAX_MSG_LEN                 = 4096;
constexpr int CAPIO_MAX_SPSQUEUE_ELEMS              = 10;
constexpr int CAPIO_MAX_SPSCQUEUE_ELEM_SIZE         = 1024 * 256;

// CAPIO streaming semantics
constexpr char CAPIO_FILE_MODE_NO_UPDATE[]           = "no_update";
constexpr char CAPIO_FILE_MODE_UPDATE[]              = "update";
constexpr char CAPIO_FILE_COMMITTED_ON_CLOSE[]       = "on_close";
constexpr char CAPIO_FILE_COMMITTED_ON_FILE[]        = "on_file";
constexpr char CAPIO_FILE_COMMITTED_ON_TERMINATION[] = "on_termination";

// CAPIO POSIX return codes
constexpr int CAPIO_POSIX_SYSCALL_ERRNO        = -1;
constexpr int CAPIO_POSIX_SYSCALL_REQUEST_SKIP = -2;
constexpr int CAPIO_POSIX_SYSCALL_SKIP         = 1;
constexpr int CAPIO_POSIX_SYSCALL_SUCCESS      = 0;

// CAPIO logger - common
constexpr char CAPIO_LOG_PRE_MSG[]        = "at[%.15lu][%.40s]: ";
constexpr char CAPIO_DEFAULT_LOG_FOLDER[] = "capio_logs\0";

// CAPIO common - shared memory constant names
constexpr char SHM_FIRST_ELEM[]        = "_first_elem_";
constexpr char SHM_LAST_ELEM[]         = "_last_elem_";
constexpr char SHM_MUTEX_PREFIX[]      = "_mutex_";
constexpr char SHM_SEM_ELEMS[]         = "_sem_num_elems_";
constexpr char SHM_SEM_EMPTY[]         = "_sem_num_empty_";
constexpr char SHM_SPSC_PREFIX_WRITE[] = "capio_write_tid_";
constexpr char SHM_SPSC_PREFIX_READ[]  = "capio_read_tid_";

// CAPIO common - shared channel by client and server
constexpr char SHM_COMM_CHAN_NAME[]      = "request_buffer";
constexpr char SHM_COMM_CHAN_NAME_RESP[] = "response_buffer_";

// CAPIO logger - shm errors
constexpr char CAPIO_SHM_OPEN_ERROR[] =
    "Unable to open shared memory segment. Could it be that another instance of capio server is "
    "running with the same WORKFLOW_NAME?";

// CAPIO logger - POSIX
constexpr char CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX[] = "posix_thread_\0";
constexpr char CAPIO_LOG_POSIX_SYSCALL_START[]           = "\n+++++++++ SYSCALL %s (%d) +++++++++";
constexpr char CAPIO_LOG_POSIX_SYSCALL_END[]             = "~~~~~~~~~  END SYSCALL ~~~~~~~~~\n";

// CAPIO logger - server
constexpr char CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX[] = "server_thread_\0";
constexpr char CAPIO_LOG_SERVER_BANNER[] =
    "\n\n "
    "\033[1;34m /$$$$$$   /$$$$$$  /$$$$$$$\033[0;96m  /$$$$$$  /$$$$$$ \n"
    "\033[1;34m /$$__  $$ /$$__  $$| $$__  $$\033[0;96m|_  $$_/ /$$__  $$\n"
    "\033[1;34m| $$  \\__/| $$  \\ $$| $$  \\ $$ \033[0;96m | $$  | $$  \\ "
    "$$\n"
    "\033[1;34m| $$      | $$$$$$$$| $$$$$$$/  \033[0;96m| $$  | $$  | $$\n"
    "\033[1;34m| $$      | $$__  $$| $$____/   \033[0;96m| $$  | $$  | $$\n"
    "\033[1;34m| $$    $$| $$  | $$| $$        \033[0;96m| $$  | $$  | $$\n"
    "\033[1;34m|  $$$$$$/| $$  | $$| $$       \033[0;96m/$$$$$$|  $$$$$$/\n"
    "\033[1;34m \\______/ |__/  |__/|__/      \033[0;96m|______/ "
    "\\______/\n\n"
    "\033[0m   CAPIO - Cross Application Programmable IO         \n"
    "                    V. " CAPIO_VERSION "\n\n";
constexpr char CAPIO_LOG_SERVER_CLI_LEVEL_INFO[]    = "[ \033[1;32m SERVER \033[0m ] ";
constexpr char CAPIO_LOG_SERVER_CLI_LEVEL_WARNING[] = "[ \033[1;33m SERVER \033[0m ] ";
constexpr char CAPIO_LOG_SERVER_CLI_LEVEL_ERROR[]   = "[ \033[1;31m SERVER \033[0m ] ";
constexpr char CAPIO_LOG_SERVER_CLI_LEVEL_JSON[]    = "[ \033[1;34m SERVER \033[0m ] ";
constexpr char CAPIO_LOG_SERVER_CLI_LOGGING_ENABLED_WARNING[] =
    "[ \033[1;33m SERVER \033[0m ] "
    "|==================================================================|\n"
    "[ \033[1;33m SERVER \033[0m ] | you are running a build of CAPIO with "
    "logging enabled.           |\n"
    "[ \033[1;33m SERVER \033[0m ] | this will have impact on performance. "
    "you "
    "should recompile CAPIO |\n"
    "[ \033[1;33m SERVER \033[0m ] | with -DCAPIO_LOG=FALSE                 "
    "   "
    "                       |\n"
    "[ \033[1;33m SERVER \033[0m ] "
    "|==================================================================|\n";
constexpr char CAPIO_LOG_SERVER_CLI_LOGGING_NOT_AVAILABLE[] =
    "CAPIO_LOG set but log support was not compiled into CAPIO!";
constexpr char CAPIO_LOG_SERVER_REQUEST_START[] = "+++++++++++ REQUEST +++++++++++";
constexpr char CAPIO_LOG_SERVER_REQUEST_END[]   = "~~~~~~~~~ END REQUEST ~~~~~~~~~\n";

// CAPIO server argument parser
constexpr char CAPIO_SERVER_ARG_PARSER_PRE[] =
    "Cross Application Programmable IO application. developed by Marco Edoardo Santimaria (UniTO), "
    "Iacopo Colonnelli(UniTO),  Massimo Torquati(UniPI) and  Marco Aldinucci (UniTO), ";
constexpr char CAPIO_SERVER_ARG_PARSER_EPILOGUE[] =
    "For further help, a full list of the available ENVIRONMENT VARIABLES,"
    " and a guide on config JSON file structure, please visit "
    "https://github.com/High-Performance-IO/capio";
constexpr char CAPIO_SERVER_ARG_PARSER_PRE_COMMAND[] = "{ENVIRONMENT_VARS} ";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_DIR_OPT_HELP[] =
    "Name of the folder to which CAPIO server will put log files into";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP[] =
    "Filename to which capio_server will log to, without extension";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP[] =
    "JSON Configuration file for capio_server";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP[] =
    "If specified, server application will start without a config file, using default settings.";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_NCONTINUE_ON_ERROR_HELP[] =
    "If specified, Capio will try to continue its execution to continue even if it has reached a "
    "fatal termination point. This flag should be used only to debug capio. If this flag is "
    "specified,  and a fatal termination point is reached, the behaviour of capio is undefined and "
    "should not  be taken as valid";

constexpr char CAPIO_LOG_SERVER_CLI_CONT_ON_ERR_WARNING[] =
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m "
    "|==================================================================|\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |           you are running CAPIO with "
    "--continue-on-error         |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |       This is extremely dangerous as CAPIO server "
    "will continue  |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |              its execution even if it should "
    "terminate.          |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |                                                     "
    "             |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |                     USE IT AT YOUR OWN RISK         "
    "             |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m |                                                     "
    "             |\033[0m\n"
    "[ \033[1;33m SERVER \033[0m ]\033[1;31m "
    "|==================================================================|\033[0m\n";

constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP[] =
    "Backend used in CAPIO. The value [backend] can be one of the following implemented backends: "
    "\n\t> mpi (default)\n\t> mpisync";

#endif // CAPIO_COMMON_CONSTANTS_HPP
