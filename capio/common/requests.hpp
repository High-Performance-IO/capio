#ifndef CAPIO_COMMON_REQUESTS_HPP
#define CAPIO_COMMON_REQUESTS_HPP

constexpr const int CAPIO_REQUEST_ACCESS              = 0;
constexpr const int CAPIO_REQUEST_CLONE               = 1;
constexpr const int CAPIO_REQUEST_CLOSE               = 2;
constexpr const int CAPIO_REQUEST_CREATE              = 3;
constexpr const int CAPIO_REQUEST_CREATE_EXCLUSIVE    = 4;
constexpr const int CAPIO_REQUEST_DUP                 = 5;
constexpr const int CAPIO_REQUEST_EXIT_GROUP          = 6;
constexpr const int CAPIO_REQUEST_FSTAT               = 7;
constexpr const int CAPIO_REQUEST_GETDENTS            = 8;
constexpr const int CAPIO_REQUEST_GETDENTS64          = 9;
constexpr const int CAPIO_REQUEST_HANDSHAKE_NAMED     = 10;
constexpr const int CAPIO_REQUEST_HANDSHAKE_ANONYMOUS = 11;
constexpr const int CAPIO_REQUEST_MKDIR               = 12;
constexpr const int CAPIO_REQUEST_OPEN                = 13;
constexpr const int CAPIO_REQUEST_READ                = 14;
constexpr const int CAPIO_REQUEST_RENAME              = 15;
constexpr const int CAPIO_REQUEST_SEEK                = 16;
constexpr const int CAPIO_REQUEST_SEEK_DATA           = 17;
constexpr const int CAPIO_REQUEST_SEEK_END            = 18;
constexpr const int CAPIO_REQUEST_SEEK_HOLE           = 19;
constexpr const int CAPIO_REQUEST_STAT                = 20;
constexpr const int CAPIO_REQUEST_UNLINK              = 21;
constexpr const int CAPIO_REQUEST_WRITE               = 22;
constexpr const int CAPIO_REQUEST_RMDIR               = 23;
constexpr const int CAPIO_REQUEST_COPY_FILE_RANGE     = 24;
constexpr const int CAPIO_REQUEST_CONSENT             = 25;
constexpr const int CAPIO_REQUEST_READ_FS             = 26;
constexpr const int CAPIO_REQUEST_PATH_PART           = 27;
constexpr const int CAPIO_REQUEST_LIST_FS             = 28;

constexpr const int CAPIO_NR_REQUESTS = 29;

/* REPLIES TO CAPIO_REQUEST_LIST_FS */
constexpr const int CAPIO_FS_LIST_AS_IS     = 0; // the kernel lists it
constexpr const int CAPIO_FS_LIST_STREAMING = 1; // more entries may come
constexpr const int CAPIO_FS_LIST_COMMITTED = 2; // what is there now is all

/* VARIANT OF THE CLIENT, IN THE HANDSHAKE */
constexpr const char CAPIO_VARIANT_MEMORY = 'M';
constexpr const char CAPIO_VARIANT_FS     = 'F';

/* REPLY TO A CLIENT OF ANOTHER VARIANT THAN ITS WORKFLOW'S */
constexpr const long long CAPIO_REPLY_VARIANT_REFUSED = -0x7ff000000000CA10LL;

/* KINDS OF CAPIO_REQUEST_CLOSE IN THE FS VARIANT */
constexpr const char CAPIO_FS_CLOSE_EXPLICIT = 'X'; // close()
constexpr const char CAPIO_FS_CLOSE_IMPLICIT = 'I'; // exit, dup2 over it, close_range
constexpr const char CAPIO_FS_CLOSE_AT_EXEC  = 'P'; // close-on-exec, if the exec works
constexpr const char CAPIO_FS_EXEC_FAILED    = 'A';
constexpr const char CAPIO_FS_PROCESS_EXIT   = 'E';

/*REQUESTS FOR SERVER TO SERVER COMMUNICATION*/

constexpr const int CAPIO_SERVER_REQUEST_READ                  = 0;
constexpr const int CAPIO_SERVER_REQUEST_READ_REPLY            = 1;
constexpr const int CAPIO_SERVER_REQUEST_STAT                  = 2;
constexpr const int CAPIO_SERVER_REQUEST_STAT_REPLY            = 3;
constexpr const int CAPIO_SERVER_REQUEST_COPY_FILE_RANGE       = 4;
constexpr const int CAPIO_SERVER_REQUEST_COPY_FILE_RANGE_REPLY = 5;
constexpr const int CAPIO_SERVER_REQUEST_LOCATION_ANNOUNCE     = 6;
constexpr const int CAPIO_SERVER_REQUEST_LOCATION_FORGET       = 7;
constexpr const int CAPIO_SERVER_REQUEST_LOCATION_QUERY        = 8;
constexpr const int CAPIO_SERVER_REQUEST_LOCATION_ANSWER       = 9;

constexpr const int CAPIO_SERVER_NR_REQUEST = 10;

#endif // CAPIO_COMMON_REQUESTS_HPP
