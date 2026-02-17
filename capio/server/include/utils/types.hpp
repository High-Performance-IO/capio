#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/queue.hpp"

typedef std::unordered_map<std::string, std::unordered_set<std::string>> CSFilesSentMap_t;
typedef std::unordered_map<int, std::unordered_map<std::string, bool>> CSWritersMap_t;
typedef std::unordered_map<std::string,
                           std::list<std::tuple<const std::filesystem::path, size_t, std::string,
                                                std::vector<std::string> *, Semaphore *>>>
    CSClientsRemotePendingNFilesMap_t;

#endif // CAPIO_SERVER_UTILS_TYPES_HPP
