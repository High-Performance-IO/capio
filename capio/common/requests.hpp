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
constexpr const int CAPIO_REQUEST_CREATE_FS           = 27;
constexpr const int CAPIO_REQUEST_OPEN_FS             = 28;

constexpr const int CAPIO_NR_REQUESTS = 29;

/* WHETHER A DESCRIPTOR OF CAPIO_REQUEST_CREATE_FS MAKES ITS CLIENT A PRODUCER */
constexpr const int CAPIO_FS_NOT_PRODUCED            = 0; ///< read-only, or inherited with fork
constexpr const int CAPIO_FS_PRODUCED                = 1; ///< created, or opened to write
constexpr const int CAPIO_FS_PRODUCED_IF_NO_PRODUCER = 2; ///< writable, inherited with exec

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
