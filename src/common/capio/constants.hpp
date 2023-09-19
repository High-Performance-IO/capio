#ifndef CAPIO_COMMON_CONSTANTS_HPP
#define CAPIO_COMMON_CONSTANTS_HPP

#include <sys/types.h>

constexpr size_t DIR_INITIAL_SIZE = 1024L * 1024 * 1024;

constexpr int DNAME_LENGTH = 128;

// default initial size for each file (can be overwritten by the user)
off64_t DEFAULT_FILE_INITIAL_SIZE = 1024L * 1024 * 1024 * 4;

// maximum size of shm
constexpr long MAX_SHM_SIZE = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
constexpr long MAX_SHM_SIZE_FILE = 1024L * 1024 * 1024 * 16;

constexpr int N_ELEMS_DATA_BUFS = 10;

constexpr int THEORETICAL_SIZE_DIRENT64 =
        sizeof(ino64_t) + sizeof(off64_t) + sizeof(unsigned short) + sizeof(unsigned char) +
        sizeof(char) * (DNAME_LENGTH + 1);

constexpr int THEORETICAL_SIZE_DIRENT =
        sizeof(unsigned long) + sizeof(off_t) + sizeof(unsigned short) + sizeof(char) * (DNAME_LENGTH + 2);

constexpr int WINDOW_DATA_BUFS = 256 * 1024;

constexpr int CAPIO_REQUEST_MAX_SIZE = 256 * sizeof(char);

constexpr int CAPIO_LOG_MAX_MSG_LEN = 2048;

constexpr int CAPIO_SEM_RETRIES = 100;
constexpr long int CAPIO_SEM_TIMEOUT_NANOSEC = 10e5;

constexpr char CAPIO_SERVER_DEFAULT_LOG_FILE_NAME[] = "server_rank_\0";

constexpr char CAPIO_SERVER_CLI_LOG_SERVER[] = "[ \033[1;32m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_WARNING[] = "[ \033[1;33m SERVER \033[0m ] ";
constexpr char CAPIO_SERVER_CLI_LOG_SERVER_ERROR[] = "[ \033[1;31m SERVER \033[0m ] ";


constexpr char CAPIO_BANNER[] = "\n\n "
                                "\033[1;34m  /$$$$$$   /$$$$$$  /$$$$$$$\033[0;96m  /$$$$$$  /$$$$$$ \n"
                                "\033[1;34m /$$__  $$ /$$__  $$| $$__  $$\033[0;96m|_  $$_/ /$$__  $$\n"
                                "\033[1;34m| $$  \\__/| $$  \\ $$| $$  \\ $$ \033[0;96m | $$  | $$  \\ $$\n"
                                "\033[1;34m| $$      | $$$$$$$$| $$$$$$$/  \033[0;96m| $$  | $$  | $$\n"
                                "\033[1;34m| $$      | $$__  $$| $$____/   \033[0;96m| $$  | $$  | $$\n"
                                "\033[1;34m| $$    $$| $$  | $$| $$        \033[0;96m| $$  | $$  | $$\n"
                                "\033[1;34m|  $$$$$$/| $$  | $$| $$       \033[0;96m/$$$$$$|  $$$$$$/\n"
                                "\033[1;34m \\______/ |__/  |__/|__/      \033[0;96m|______/ \\______/\n\n"
                                "\033[0m   CAPIO - Cross Application Programmable IO server         \n\n";


//constant strings for argument parser and capio server help
constexpr char CAPIO_SERVER_ARG_PARSER_PRE[] = "Cross Application IO server application. developed by Alberto Riccardo martinelli (UniTO), "
                                               "Massimo Torquati(UniPI), Marco Aldinucci (UniTO), Iacopo Colonneli(UniTO) and"
                                               " Marco Edoardo Santimaria (UniTO).";
constexpr char CAPIO_SERVER_ARG_PARSER_EPILOGUE[] = "For futher help, a full list of the available ENVIROMENT VARIABLES,"
                                                    " and a guide on config JSON file structure, please visit "
                                                    "https://github.com/High-Performance-IO/capio";
constexpr char CAPIO_SERVER_ARG_PARSER_PRE_COMMAND[] = "{ENVIROMENT_VARS}  mpirun -n 1";
constexpr char CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP[] = "Filename to which capio_server will log to, without extension";
constexpr char CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP[] = "JSON Configuration file for capio_server";

#endif // CAPIO_COMMON_CONSTANTS_HPP
