#include <arpa/inet.h>
#include <sys/socket.h>

#include "common/logger.hpp"
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
            if (std::find(cache.begin(), cache.end(), entry.path()) == cache.end()) {
                cache.push_back(entry.path());

                // Read connection token from FS
                std::ifstream input(entry.path());
                std::string token;
                input >> token;

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
    std::ofstream token_file(token_directory_path / token_filename);
    token_file << advertisement_token;
    token_file.close();

    fs_listener_thread =
        new std::thread(fs_discovery_service, &terminate, token_directory_path, adv_delay);
}

void FSDiscoveryInterface::stop() {
    terminate = true;

    if (fs_listener_thread != nullptr && fs_listener_thread->joinable()) {
        fs_listener_thread->join();
        fs_listener_thread = nullptr;
        server_println("Discovery service stopped.", CapioCLEngine::get().getWorkflowName(),
                       CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "FSDiscoveryService");
    }
}

FSDiscoveryInterface::~FSDiscoveryInterface() {
    // delete aliveness token
    if (!token_filename.empty()) {
        std::filesystem::remove(token_directory_path / token_filename);
    }
}
