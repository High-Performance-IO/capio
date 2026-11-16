#include <arpa/inet.h>
#include <sys/socket.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "server/include/remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

extern Backend *backend;

void fs_discovery_service(const bool *terminate, const std::filesystem::path &token_directory_path,
                          const unsigned int delay_ms) {
    // local cache to not reload tokens already found
    // TODO: relax this by storing also last modified date, and reload in case changes occurred
    //       after first read

    std::vector<std::filesystem::path> cache;

    while (!*terminate) {
        const auto iterator = std::filesystem::directory_iterator(token_directory_path);
        for (auto &entry : iterator) {
            // Only published tokens: "<host>.capio" (a hidden file is still being written).
            const std::string name = entry.path().filename().string();
            if (name.empty() || name.front() == '.' || entry.path().extension() != ".capio") {
                continue;
            }
            if (std::find(cache.begin(), cache.end(), entry.path()) == cache.end()) {
                // Read connection token from FS
                std::ifstream input(entry.path());
                std::string token;
                input >> token;
                if (token.empty()) {
                    // Not readable yet (e.g. a token written in place by an older server):
                    // look again at the next scan instead of ignoring this peer forever.
                    continue;
                }
                cache.push_back(entry.path());

                // Send token to backend to issue a direct connection.
                // NOTE: backend will refuse to connect silently if connection is already
                // established
                backend->connect_to(token);
            }
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

    token_filename      = node_name + ".capio";
    advertisement_token = token;
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
