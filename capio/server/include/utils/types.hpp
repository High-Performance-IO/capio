#ifndef CAPIO_SERVER_UTILS_TYPES_HPP
#define CAPIO_SERVER_UTILS_TYPES_HPP

#include <filesystem>
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

typedef void (*CSHandler_t)(const char *const);

/**
 * A copy_file_range request of a client between two CAPIO files: the server copies the data from
 * the buffer of the input to the buffer of the output, and the data never goes to the client and
 * back. A request copies at most up to the end of the data that follows the input position, as a
 * read does, and the client asks again for the rest. The server does not move the file offsets of
 * the descriptors: the client sets them once the copy is done.
 */
struct CSCopyFileRange_t {
    int tid;                        ///< Thread of the client that asked for the copy
    int fd_in;                      ///< Descriptor of the input
    int fd_out;                     ///< Descriptor of the output
    off64_t offset_in;              ///< Position of the copy in the input
    off64_t offset_out;             ///< Position of the copy in the output
    off64_t count;                  ///< Largest number of bytes to copy
    std::filesystem::path path_in;  ///< Path of the input
    std::filesystem::path path_out; ///< Path of the output
};

#endif // CAPIO_SERVER_UTILS_TYPES_HPP
