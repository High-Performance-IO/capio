#ifndef CAPIO_COMMON_CONSTANTS_HPP
#define CAPIO_COMMON_CONSTANTS_HPP

#include <array>
#include <climits>
#include <string_view>

#include <sys/types.h>

// CAPIO files constants
constexpr size_t CAPIO_DEFAULT_DIR_INITIAL_SIZE   = 1024L * 1024 * 1024;
constexpr off64_t CAPIO_DEFAULT_FILE_INITIAL_SIZE = 1024L * 1024 * 1024 * 4;
constexpr std::array CAPIO_DIR_FORBIDDEN_PATHS    = {std::string_view{"/proc/"},
                                                     std::string_view{"/sys/"}};
constexpr int CAPIO_THEORETICAL_SIZE_DIRENT64     = sizeof(ino64_t) + sizeof(off64_t) +
                                                sizeof(unsigned short) + sizeof(unsigned char) +
                                                sizeof(char) * NAME_MAX;

// CAPIO semaphore constants
constexpr int CAPIO_SEM_MAX_RETRIES          = 100;
constexpr long int CAPIO_SEM_TIMEOUT_NANOSEC = 10e5;

// CAPIO default values for shared memory
constexpr char CAPIO_DEFAULT_WORKFLOW_NAME[] = "CAPIO";
constexpr char CAPIO_SHM_CANARY_ERROR[] =
    "FATAL ERROR:  Shared memories for workflow %s already "
    "exists. One of two (or both) reasons are to blame: \n             "
    "either a previous run of CAPIO terminated without "
    "cleaning up memory, or another instance of CAPIO\n             "
    "is already running. Clean shared memory and then retry";

// CAPIO communication constants
constexpr int CAPIO_DATA_BUFFER_LENGTH               = 10;
constexpr int CAPIO_DATA_BUFFER_ELEMENT_SIZE         = 256 * 1024;
constexpr size_t CAPIO_SERVER_REQUEST_MAX_SIZE       = sizeof(char) * (PATH_MAX + 81920);
constexpr size_t CAPIO_REQUEST_MAX_SIZE              = 256 * sizeof(char);
constexpr char CAPIO_SERVER_CLI_LOG_SERVER[]         = "[ \033[1;32m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_WARNING[] = "[ \033[1;33m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_ERROR[]   = "[ \033[1;31m SERVER \033[0m ] ";
constexpr char LOG_CAPIO_START_REQUEST[]             = "\n+++++++++++ SYSCALL %s (%d) +++++++++++";
constexpr char LOG_CAPIO_END_REQUEST[]               = "----------- END SYSCALL ----------\n";
constexpr char CAPIO_SERVER_LOG_START_REQUEST_MSG[]  = "+++++++++++++++++REQUEST+++++++++++++++++";
constexpr char CAPIO_SERVER_LOG_END_REQUEST_MSG[]    = "~~~~~~~~~~~~~~~END REQUEST~~~~~~~~~~~~~~~";
constexpr int N_ELEMS_DATA_BUFS                      = 10;
constexpr int WINDOW_DATA_BUFS                       = 256 * 1024;
constexpr int CAPIO_LOG_MAX_MSG_LEN                  = 2048;
constexpr int CAPIO_SEM_RETRIES                      = 100;
constexpr int THEORETICAL_SIZE_DIRENT64              = sizeof(ino64_t) + sizeof(off64_t) +
                                          sizeof(unsigned short) + sizeof(unsigned char) +
                                          sizeof(char) * NAME_MAX;

// CAPIO streaming semantics
constexpr char CAPIO_FILE_MODE_NO_UPDATE[]           = "no_update";
constexpr char CAPIO_FILE_MODE_UPDATE[]              = "update";
constexpr char CAPIO_FILE_COMMITTED_ON_CLOSE[]       = "on_close";
constexpr char CAPIO_FILE_COMMITTED_ON_TERMINATION[] = "on_termination";

// CAPIO POSIX return codes
constexpr int CAPIO_POSIX_SYSCALL_ERRNO        = -1;
constexpr int CAPIO_POSIX_SYSCALL_REQUEST_SKIP = -2;
constexpr int CAPIO_POSIX_SYSCALL_SKIP         = 1;
constexpr int CAPIO_POSIX_SYSCALL_SUCCESS      = 0;

// CAPIO logger - common
constexpr char CAPIO_LOG_PRE_MSG[]        = "at[%s]: ";
constexpr char CAPIO_DEFAULT_LOG_FOLDER[] = "capio_logs\0";

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
constexpr char CAPIO_LOG_SERVER_REQUEST_START[] = "\n+++++++++++ REQUEST +++++++++++";
constexpr char CAPIO_LOG_SERVER_REQUEST_END[]   = "~~~~~~~~~ END REQUEST ~~~~~~~~~\n";

// CAPIO server argument parser
constexpr char CAPIO_SERVER_ARG_PARSER_PRE[] =
    "Cross Application Programmable IO application. developed by Alberto "
    "Riccardo Martinelli (UniTO), Massimo Torquati(UniPI), Marco Aldinucci (UniTO), Iacopo "
    "Colonnelli(UniTO)  and Marco Edoardo Santimaria (UniTO).";
constexpr char CAPIO_SERVER_ARG_PARSER_EPILOGUE[] =
    "For further help, a full list of the available ENVIRONMENT VARIABLES,"
    " and a guide on config JSON file structure, please visit "
    "https://github.com/High-Performance-IO/capio";
constexpr char CAPIO_SERVER_ARG_PARSER_PRE_COMMAND[] = "{ENVIRONMENT_VARS}  mpirun -n 1";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_DIR_OPT_HELP[] =
    "Name of the folder to which CAPIO server will put log files into";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP[] =
    "Filename to which capio_server will log to, without extension";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP[] =
    "JSON Configuration file for capio_server";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP[] =
    "If specified, server application will start without a config file, using default settings.";

constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP[] =
    "Backend used in CAPIO. The value [backend] can be one of the following implemented backends: "
    "\n\t> mpi (default)\n\t> mpisync";

#endif // CAPIO_COMMON_CONSTANTS_HPP
