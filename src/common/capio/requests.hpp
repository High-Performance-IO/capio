#ifndef CAPIO_COMMON_REQUESTS_HPP
#define CAPIO_COMMON_REQUESTS_HPP

constexpr const int CAPIO_REQUEST_CONSENT             = 0;
constexpr const int CAPIO_REQUEST_ACCESS              = 1;
constexpr const int CAPIO_REQUEST_CLONE               = 2;
constexpr const int CAPIO_REQUEST_CLOSE               = 3;
constexpr const int CAPIO_REQUEST_CREATE              = 4;
constexpr const int CAPIO_REQUEST_EXIT_GROUP          = 5;
constexpr const int CAPIO_REQUEST_GETDENTS            = 6;
constexpr const int CAPIO_REQUEST_GETDENTS64          = 7;
constexpr const int CAPIO_REQUEST_HANDSHAKE_NAMED     = 8;
constexpr const int CAPIO_REQUEST_HANDSHAKE_ANONYMOUS = 9;
constexpr const int CAPIO_REQUEST_MKDIR               = 10;
constexpr const int CAPIO_REQUEST_OPEN                = 11;
constexpr const int CAPIO_REQUEST_READ                = 12;
constexpr const int CAPIO_REQUEST_RENAME              = 13;
constexpr const int CAPIO_REQUEST_SEEK                = 14;
constexpr const int CAPIO_REQUEST_STAT                = 15;
constexpr const int CAPIO_REQUEST_UNLINK              = 16;
constexpr const int CAPIO_REQUEST_WRITE               = 17;
constexpr const int CAPIO_REQUEST_RMDIR               = 18;

constexpr const int CAPIO_NR_REQUESTS = 19;

#endif // CAPIO_COMMON_REQUESTS_HPP
