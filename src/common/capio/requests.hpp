#ifndef CAPIO_COMMON_REQUESTS_HPP
#define CAPIO_COMMON_REQUESTS_HPP

constexpr const int CAPIO_REQUEST_CONSENT        = 0;
constexpr const int CAPIO_REQUEST_CLOSE          = 1;
constexpr const int CAPIO_REQUEST_CREATE         = 2;
constexpr const int CAPIO_REQUEST_EXIT_GROUP     = 3;
constexpr const int CAPIO_REQUEST_HANDSHAKE      = 4;
constexpr const int CAPIO_REQUEST_MKDIR          = 5;
constexpr const int CAPIO_REQUEST_OPEN           = 6;
constexpr const int CAPIO_REQUEST_READ           = 7;
constexpr const int CAPIO_REQUEST_READ_MEM       = 8;
constexpr const int CAPIO_REQUEST_RENAME         = 9;
constexpr const int CAPIO_REQUEST_WRITE          = 10;
constexpr const int CAPIO_REQUEST_WRITE_MEM      = 11;
constexpr const int CAPIO_REQUEST_QUERY_MEM_FILE = 12;

constexpr const int CAPIO_NR_REQUESTS = 13;

#endif // CAPIO_COMMON_REQUESTS_HPP
