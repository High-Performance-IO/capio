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

constexpr const int CAPIO_NR_REQUESTS = 24;

/*REQUESTS FOR SERVER TO SERVER COMMUNICATION*/

constexpr const int CAPIO_SERVER_REQUEST_READ             = 0;
constexpr const int CAPIO_SERVER_REQUEST_READ_REPLY       = 1;
constexpr const int CAPIO_SERVER_REQUEST_READ_BATCH       = 2;
constexpr const int CAPIO_SERVER_REQUEST_READ_BATCH_REPLY = 3;
constexpr const int CAPIO_SERVER_REQUEST_STAT             = 4;
constexpr const int CAPIO_SERVER_REQUEST_STAT_REPLY       = 5;

constexpr const int CAPIO_SERVER_NR_REQUEST = 6;

#endif // CAPIO_COMMON_REQUESTS_HPP
