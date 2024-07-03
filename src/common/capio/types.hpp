#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capio/queue.hpp"

#include "capio_file.hpp"

typedef std::unordered_map<int, int> CSPidsMap_T;
typedef std::unordered_map<int, std::string> CSAppsMap_t;
typedef std::unordered_map<std::string, std::unordered_set<std::string>> CSFilesSentMap_t;
typedef std::unordered_map<int, std::unordered_map<int, std::filesystem::path>>
    CSProcessFileMetadataMap_t;
typedef std::unordered_map<int, std::pair<SPSCQueue *, SPSCQueue *>> CSDataBufferMap_t;
typedef std::unordered_map<std::string, CapioFile *> CSFilesMetadata_t;
typedef std::unordered_map<
    std::string, std::tuple<std::string, std::string, std::string, long int, bool, long int>>
    CSMetadataConfMap_t;
typedef std::vector<std::tuple<std::string, std::string, std::string, std::string, long int,
                               long int, bool, long int>>
    CSMetadataConfGlobs_t;
typedef std::unordered_map<int, std::unordered_map<std::string, bool>> CSWritersMap_t;
typedef std::unordered_map<std::string,
                           std::list<std::tuple<const std::filesystem::path, size_t, std::string,
                                                std::vector<std::string> *, Semaphore *>>>
    CSClientsRemotePendingNFilesMap_t;
typedef std::unordered_map<int, CircularBuffer<off_t> *> CSBufResponse_t;
typedef CircularBuffer<char> CSBufRequest_t;

typedef void (*CSHandler_t)(const char *const);

#endif // CAPIO_SERVER_UTILS_TYPES_HPP
