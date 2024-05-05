#ifndef CAPIO_POSIX_UTILS_TYPES_HPP
#define CAPIO_POSIX_UTILS_TYPES_HPP

#include <unordered_map>
#include <unordered_set>

#include "capio/queue.hpp"

typedef std::unordered_map<int, std::tuple<std::shared_ptr<off64_t>, off64_t, int, bool>> CPFiles_t;
typedef std::pair<off64_t, off64_t> CPStatResponse_t;
typedef std::unordered_map<long, CircularBuffer<off_t> *> CPBufResponse_t;
typedef std::unordered_map<int, std::string> CPFileDescriptors_t;
typedef std::unordered_map<std::string, std::unordered_set<int>> CPFilesPaths_t;

typedef int (*CPHandler_t)(long, long, long, long, long, long, long *);

#endif // CAPIO_POSIX_UTILS_TYPES_HPP
