#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capio/circular_buffer.hpp"
#include "capio/spsc_queue.hpp"

typedef std::unordered_map<int, int> CSPidsMap_T;
typedef std::unordered_map<int, std::string> CSAppsMap_t;
typedef std::unordered_map<std::string, std::unordered_set<std::string>> CSFilesSentMap_t;
typedef std::unordered_map<int, std::unordered_map<int, std::pair<CapioFile *, off64_t *>>>
    CSProcessFileMap_t;
typedef std::unordered_map<int, std::unordered_map<int, std::filesystem::path>>
    CSProcessFileMetadataMap_t;
typedef std::unordered_map<int, std::pair<SPSCQueue<char> *, SPSCQueue<char> *>> CSDataBufferMap_t;
typedef std::unordered_map<std::string, CapioFile *> CSFilesMetadata_t;
typedef std::unordered_map<
    std::string, std::tuple<std::string, std::string, std::string, long int, bool, long int>>
    CSMetadataConfMap_t;
typedef std::vector<std::tuple<std::string, std::string, std::string, std::string, long int,
                               long int, bool, long int>>
    CSMetadataConfGlobs_t;
typedef std::unordered_map<int, std::unordered_map<std::string, bool>> CSWritersMap_t;
typedef std::unordered_map<std::string, std::pair<const char *const, off64_t>> CSFilesLocationMap_t;
typedef std::unordered_map<std::string, int> CSNodesHelperRankMap_t;
typedef std::unordered_map<int, std::string> CSRankToNodeMap_t;
typedef std::unordered_map<std::string,
                           std::list<std::tuple<const std::filesystem::path, size_t, int,
                                                std::vector<std::string> *, sem_t *>>>
    CSClientsRemotePendingNFilesMap_t;
typedef std::unordered_map<int, CircularBuffer<off_t> *> CSBufResponse_t;
typedef CircularBuffer<char> CSBufRequest_t;

typedef void (*CSHandler_t)(const char *const, int);

#endif // CAPIO_SERVER_UTILS_TYPES_HPP
