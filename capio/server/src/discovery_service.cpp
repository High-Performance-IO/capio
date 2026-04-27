#include <arpa/inet.h>
#include <sys/socket.h>

#include "common/logger.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

extern Backend *backend;

// constant required by setsockopt()
int REUSE_MCAST_SOCKET = 1;

void advertise(const bool *terminate, const unsigned int delay_ms,
               const std::string &advertisement_token, const std::string &adv_addr,
               const unsigned int adv_port) {
    const int advert_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in advert_multicast_addr{};
    advert_multicast_addr.sin_family      = AF_INET;
    advert_multicast_addr.sin_port        = htons(adv_port);
    advert_multicast_addr.sin_addr.s_addr = inet_addr(adv_addr.c_str());

    while (!*terminate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        sendto(advert_sock_fd, advertisement_token.data(), advertisement_token.size(), 0,
               reinterpret_cast<sockaddr *>(&advert_multicast_addr), sizeof(advert_multicast_addr));
    }

    close(advert_sock_fd);
}

void mcast_thread_discovery_service(const bool *terminate, const std::string &adv_addr,
                                    const unsigned int adv_port) {
    START_LOG(gettid(), "call()");

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &REUSE_MCAST_SOCKET, sizeof(REUSE_MCAST_SOCKET));

    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000; // 100,000 microseconds = 100ms
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in local_addr{};
    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(adv_port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) == -1) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Error: unable to bind to multicast socket. Error is: " +
                           std::string(std::strerror(errno)));
        // halt execution and return
        return;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(adv_addr.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    char incoming_token[2 * HOST_NAME_MAX] = {0};

    while (!*terminate) {

        bzero(incoming_token, 2 * HOST_NAME_MAX);

        if (recvfrom(sockfd, incoming_token, sizeof(incoming_token) - 1, 0, nullptr, nullptr) > 0) {
            backend->connect_to(incoming_token);
        }
    }
    close(sockfd);
}

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

void DiscoveryService::start(unsigned int adv_delay, const std::string &token,
                             const std::string &token_directory) {

    if (token.empty()) {
        throw std::runtime_error("Advertisement token is empty");
    }

    if (token_directory.empty()) {
        throw std::runtime_error("Provided token directory is empty");
    }

    if (!std::filesystem::exists(token_directory)) {
        std::filesystem::create_directory(token_directory);
    }

    std::string node_name(HOST_NAME_MAX, '\0');
    gethostname(node_name.data(), node_name.size());
    node_name.resize(strlen(node_name.data()));

    token_directory_path = token_directory;
    token_filename       = node_name + ".capio";
    advertisement_token  = token;

    std::ofstream token_file(token_directory_path / token_filename);
    token_file << advertisement_token;
    token_file.close();

    mcast_listener_thread = new std::thread(mcast_thread_discovery_service, &terminate,
                                            capio_multicast_adv_address, capio_multicast_adv_port);
    fs_listener_thread =
        new std::thread(fs_discovery_service, &terminate, token_directory_path, adv_delay);
    advertisement_thread =
        new std::thread(advertise, &terminate, adv_delay, std::ref(advertisement_token),
                        capio_multicast_adv_address, capio_multicast_adv_port);

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "DiscoveryService will advertise " +
                                                        advertisement_token + " every " +
                                                        std::to_string(adv_delay) + "ms.");
}
void DiscoveryService::stop() {
    terminate = true;

    if (mcast_listener_thread != nullptr && mcast_listener_thread->joinable()) {
        mcast_listener_thread->join();
        mcast_listener_thread = nullptr;
    }

    if (fs_listener_thread != nullptr && fs_listener_thread->joinable()) {
        fs_listener_thread->join();
        fs_listener_thread = nullptr;
    }

    if (advertisement_thread != nullptr && advertisement_thread->joinable()) {
        advertisement_thread->join();
        advertisement_thread = nullptr;
    }
}

DiscoveryService::DiscoveryService(const std::string &mcast_addr, const unsigned int mcast_port)
    : capio_multicast_adv_address(mcast_addr), capio_multicast_adv_port(mcast_port) {
    shm_canary = new CapioShmCanary(CapioCLEngine::get().getWorkflowName());
}

DiscoveryService::~DiscoveryService() {
    // if destructor is called before stop(), then stop the the service first.
    if (!terminate) {
        stop();
    }
    // delete aliveness token
    std::filesystem::remove(token_directory_path / token_filename);

    // delete shm canary
    delete shm_canary;
}