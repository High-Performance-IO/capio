#include <fstream>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "server/include/remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/server_state.hpp"

namespace {
bool same_time(const timespec &lhs, const timespec &rhs) {
    return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
}
} // namespace

std::vector<std::string> FsTokenCache::scan(const std::filesystem::path &directory) {
    START_LOG(gettid(), "call(directory=%s)", directory.c_str());
    std::vector<std::string> found;
    std::unordered_map<std::string, Entry> seen;

    for (const auto &file : std::filesystem::directory_iterator(directory)) {
        // Only published tokens: "<host>.capio" (a hidden file is still being written).
        const std::string name = file.path().filename().string();
        if (name.empty() || name.front() == '.' || file.path().extension() != ".capio") {
            continue;
        }
        struct stat info{};
        if (stat(file.path().c_str(), &info) != 0) {
            continue; // removed since it was listed
        }
        Entry entry{info.st_ino, info.st_mtim, info.st_size, {}};

        const auto cached = _entries.find(file.path());
        if (cached != _entries.end() && cached->second.inode == entry.inode &&
            same_time(cached->second.modified, entry.modified) &&
            cached->second.size == entry.size) {
            seen.emplace(file.path(), std::move(cached->second));
            continue;
        }

        std::ifstream input(file.path());
        input >> entry.token;
        if (entry.token.empty()) {
            // Not readable yet (e.g. a token written in place by an older server): look again at
            // the next scan instead of ignoring this peer forever. What was read before is kept.
            if (cached != _entries.end()) {
                seen.emplace(file.path(), std::move(cached->second));
            }
            continue;
        }
        if (cached == _entries.end() || cached->second.token != entry.token) {
            LOG("New token in %s: %s", file.path().c_str(), entry.token.c_str());
            found.push_back(entry.token);
        }
        seen.emplace(file.path(), std::move(entry));
    }
    // A token file that is gone is forgotten: if it comes back, its token is read again
    _entries = std::move(seen);
    return found;
}

std::string FsTokenCache::token(const std::filesystem::path &token_file) const {
    const auto entry = _entries.find(token_file);
    return entry == _entries.end() ? std::string{} : entry->second.token;
}

void fs_discovery_service(const std::atomic_bool *terminate,
                          const std::filesystem::path &token_directory_path,
                          const unsigned int delay_ms) {
    FsTokenCache cache;

    while (!*terminate) {
        for (const auto &token : cache.scan(token_directory_path)) {
            // Send token to backend to issue a direct connection.
            // NOTE: backend will refuse to connect silently if connection is already
            // established
            backend->connect_to(token);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

FSDiscoveryInterface::FSDiscoveryInterface(const std::string &token_directory) {
    if (token_directory.empty()) {
        throw std::runtime_error("Provided token directory is empty");
    }

    if (!std::filesystem::exists(token_directory)) {
        std::filesystem::create_directory(token_directory);
    }

    token_directory_path = token_directory;
}

void FSDiscoveryInterface::start(const std::string &token, unsigned int adv_delay) {
    std::string node_name(HOST_NAME_MAX, '\0');
    gethostname(node_name.data(), node_name.size());
    node_name.resize(strlen(node_name.data()));

    token_filename        = node_name + ".capio";
    advertisement_token   = token;
    // Publish the token atomically: on a shared file system the other servers may list the
    // directory between the creation of the file and the write of its content.
    const auto token_path = token_directory_path / token_filename;
    const auto tmp_path   = token_directory_path / ("." + token_filename.string() + ".tmp");
    {
        std::ofstream token_file(tmp_path);
        token_file << advertisement_token;
        token_file.close();
        if (!token_file) {
            throw std::runtime_error("Unable to write the discovery token " + tmp_path.string());
        }
    }
    std::filesystem::rename(tmp_path, token_path);

    fs_listener_thread =
        new std::thread(fs_discovery_service, &terminate, token_directory_path, adv_delay);
}

void FSDiscoveryInterface::stop() {
    terminate = true;

    if (fs_listener_thread != nullptr && fs_listener_thread->joinable()) {
        fs_listener_thread->join();
        delete fs_listener_thread;
        fs_listener_thread = nullptr;
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Discovery service stopped.");
    }
}

FSDiscoveryInterface::~FSDiscoveryInterface() {
    // delete aliveness token
    if (!token_filename.empty()) {
        std::filesystem::remove(token_directory_path / token_filename);
    }
}
