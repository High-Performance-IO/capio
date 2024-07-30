#ifndef CAPIO_COMMON_REQUESTS_HPP
#define CAPIO_COMMON_REQUESTS_HPP

constexpr const int CAPIO_REQUEST_CONSENT             = 0;
constexpr const int CAPIO_REQUEST_CLONE               = 1;
constexpr const int CAPIO_REQUEST_CLOSE               = 2;
constexpr const int CAPIO_REQUEST_CREATE              = 3;
constexpr const int CAPIO_REQUEST_EXIT_GROUP          = 4;
constexpr const int CAPIO_REQUEST_HANDSHAKE_NAMED     = 5;
constexpr const int CAPIO_REQUEST_HANDSHAKE_ANONYMOUS = 6;
constexpr const int CAPIO_REQUEST_MKDIR               = 7;
constexpr const int CAPIO_REQUEST_OPEN                = 8;
constexpr const int CAPIO_REQUEST_READ                = 9;
constexpr const int CAPIO_REQUEST_RENAME              = 10;
constexpr const int CAPIO_REQUEST_WRITE               = 11;

constexpr const int CAPIO_NR_REQUESTS = 12;

#endif // CAPIO_COMMON_REQUESTS_HPP
