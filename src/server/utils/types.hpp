#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capio/queue.hpp"

typedef std::unordered_map<int, CircularBuffer<capio_off64_t> *> CSBufResponse_t;
typedef CircularBuffer<char> CSBufRequest_t;

typedef void (*CSHandler_t)(const char *const);

#endif // CAPIO_SERVER_UTILS_TYPES_HPP
