#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capio/queue.hpp"

/**
 * @brief Typedef to store the map of response buffers indexed by process tid
 *
 */
typedef std::unordered_map<int, CircularBuffer<capio_off64_t> *> CSBufResponse_t;

/**
 * @brief Typedef for the shared memory queue object for incoming requests
 *
 */
typedef CircularBuffer<char> CSBufRequest_t;

/**
 * @brief Typedef for the generic capio_server systemcall handler. Required to create direct access
 * array of handlers with common interface
 *
 */
typedef void (*CSHandler_t)(const char *const);

#endif // CAPIO_SERVER_UTILS_TYPES_HPP