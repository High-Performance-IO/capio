#ifndef CAPIO_COMMON_CONSTANTS_HPP
#define CAPIO_COMMON_CONSTANTS_HPP

#include <climits>
#include <sys/types.h>

constexpr size_t DIR_INITIAL_SIZE = 1024L * 1024 * 1024;

constexpr int DNAME_LENGTH = 128;

// default initial size for each file (can be overwritten by the user)
off64_t DEFAULT_FILE_INITIAL_SIZE = 1024L * 1024 * 1024 * 4;

// maximum size of shm
constexpr long MAX_SHM_SIZE = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
constexpr long MAX_SHM_SIZE_FILE = 1024L * 1024 * 1024 * 16;

// capio file mode
constexpr char CAPIO_FILE_MODE_NO_UPDATE[]           = "no_update";
constexpr char CAPIO_FILE_MODE_ON_CLOSE[]            = "on_close";
constexpr char CAPIO_FILE_MODE_ON_TERMINATION[]      = "on_termination";
constexpr char CAPIO_SERVER_DEFAULT_LOG_FILE_NAME[]  = "server_rank_\0";
constexpr char CAPIO_APP_LOG_FILE_NAME[]             = "/dev/stderr\0";
constexpr char LOG_PRE_MSG[]                         = "tid[%ld]-at[%s]: ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER[]         = "[ \033[1;32m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_WARNING[] = "[ \033[1;33m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_ERROR[]   = "[ \033[1;31m SERVER \033[0m ] ";
constexpr char LOG_CAPIO_START_REQUEST[]             = "\n+++++++++++ SYSCALL %s (%d) +++++++++++";
constexpr char LOG_CAPIO_END_REQUEST[]               = "----------- END SYSCALL ----------\n";
constexpr char CAPIO_SERVER_LOG_START_REQUEST_MSG[]  = "+++++++++++++++++REQUEST+++++++++++++++++";
constexpr char CAPIO_SERVER_LOG_END_REQUEST_MSG[]    = "~~~~~~~~~~~~~~~END REQUEST~~~~~~~~~~~~~~~";
constexpr long int CAPIO_SEM_TIMEOUT_NANOSEC         = 10e5;
constexpr int N_ELEMS_DATA_BUFS                      = 10;
constexpr int WINDOW_DATA_BUFS                       = 256 * 1024;
constexpr int CAPIO_REQUEST_MAX_SIZE                 = 256 * sizeof(char);
constexpr int CAPIO_LOG_MAX_MSG_LEN                  = 2048;
constexpr int CAPIO_SEM_RETRIES                      = 100;
constexpr int THEORETICAL_SIZE_DIRENT64              = sizeof(ino64_t) + sizeof(off64_t) +
                                          sizeof(unsigned short) + sizeof(unsigned char) +
                                          sizeof(char) * NAME_MAX;

constexpr int POSIX_SYSCALL_HANDLED_BY_CAPIO            = 0;
constexpr int POSIX_SYSCALL_HANDLED_BY_CAPIO_SET_ERRNO  = -1;
constexpr int POSIX_SYSCALL_TO_HANDLE_BY_KERNEL         = 1;
constexpr int POSIX_REQUEST_SYSCALL_TO_HANDLE_BY_KERNEL = -2;

constexpr char CAPIO_BANNER[] =
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

constexpr char CAPIO_LOG_CLI_WARNING[] =
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

constexpr char CAPIO_LOG_CLI_WARNING_LOG_SET_NOT_COMPILED[] =
    "CAPIO_LOG set but log support was not compiled into CAPIO!";

// constant strings for argument parser and capio server help
constexpr char CAPIO_SERVER_ARG_PARSER_PRE[] =
    "Cross Application Programmable IO application. developed by Alberto "
    "Riccardo Martinelli (UniTO), Massimo Torquati(UniPI), Marco Aldinucci (UniTO), Iacopo "
    "Colonneli(UniTO)  and Marco Edoardo Santimaria (UniTO).";
constexpr char CAPIO_SERVER_ARG_PARSER_EPILOGUE[] =
    "For further help, a full list of the available ENVIRONMENT VARIABLES,"
    " and a guide on config JSON file structure, please visit "
    "https://github.com/High-Performance-IO/capio";
constexpr char CAPIO_SERVER_ARG_PARSER_PRE_COMMAND[] = "{ENVIRONMENT_VARS}  mpirun -n 1";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP[] =
    "Filename to which capio_server will log to, without extension";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP[] =
    "JSON Configuration file for capio_server";

constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP[] =
    "If specified, server application will start without a config file, using default settings.";
#endif // CAPIO_COMMON_CONSTANTS_HPP
