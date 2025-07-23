#ifndef CAPIOCOMMUNICATIONSERVICE_HPP
#define CAPIOCOMMUNICATIONSERVICE_HPP

#include "BackendInterface.hpp"
#include "MTCL_backend.hpp"

#include <algorithm>
#include <filesystem>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

class CapioCommunicationService {

    char ownHostname[HOST_NAME_MAX] = {0};
    bool *continue_execution        = new bool;
    std::thread *thread_server_finder_fs, *thread_server_finder_multicast;

    std::vector<std::string> token_used_to_connect;
    std::mutex *token_used_to_connect_mutex;

    void generate_aliveness_token(const int port) const {
        START_LOG(gettid(), "call(port=d)", port);

        std::string token_filename(ownHostname);
        token_filename += ".alive_connection";

        LOG("Creating alive token %s", token_filename.c_str());

        std::ofstream FilePort(token_filename);
        FilePort << port;
        FilePort.close();

        LOG("Saved self token info to FS");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << ownHostname << " ] "
                  << "Generated token at " << token_filename << std::endl;
    }

    void delete_aliveness_token() const {
        START_LOG(gettid(), "call()");

        std::string token_filename(ownHostname);
        token_filename += ".alive_connection";
        if (!std::filesystem::exists(token_filename)) {
            LOG("Token does not exists. Skipping delettion");
            return;
        }

        LOG("Removing alive token %s", token_filename.c_str());
        std::filesystem::remove(token_filename);
        LOG("Removed token");
    }

    static void send_multicast_alive_token(int backend_port) {
        char hostname[HOST_NAME_MAX];
        gethostname(hostname, HOST_NAME_MAX);

        int transmission_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (transmission_socket < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << hostname << " ] "
                      << "WARNING: unable to bind multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << hostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        addr.sin_port        = htons(MULTICAST_DISCOVERY_PORT);

        char message[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
        sprintf(message, "%s:%d", hostname, backend_port);

        if (sendto(transmission_socket, message, strlen(message), 0,
                   reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << hostname << " ] "
                      << "WARNING: unable to send alive token(" << message
                      << ") to multicast address!: " << strerror(errno) << std::endl;
        }
        close(transmission_socket);
    }

    static void find_new_server_from_multicast_thread(
        const bool *continue_execution, std::vector<std::string> *token_used_to_connect,
        std::mutex *token_used_to_connect_mutex, int backend_port) {
        char incomingMessage[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
        char ownHostname[HOST_NAME_MAX];
        gethostname(ownHostname, HOST_NAME_MAX);

        int discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (discovery_socket < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to open multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        u_int multiple_socket_on_same_address = 1;
        if (setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR,
                       (char *) &multiple_socket_on_same_address,
                       sizeof(multiple_socket_on_same_address)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to assign multiple sockets to same address: "
                      << strerror(errno) << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        struct sockaddr_in addr = {};
        addr.sin_family         = AF_INET;
        addr.sin_addr.s_addr    = htonl(INADDR_ANY);
        addr.sin_port           = htons(MULTICAST_DISCOVERY_PORT);
        socklen_t addrlen       = sizeof(addr);

        // bind to receive address
        if (bind(discovery_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to bind multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to join multicast group: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        while (*continue_execution) {
            bzero(incomingMessage, sizeof(incomingMessage));
            send_multicast_alive_token(backend_port);
            if (recvfrom(discovery_socket, incomingMessage, MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE, 0,
                         reinterpret_cast<sockaddr *>(&addr), &addrlen) < 0) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                          << "WARNING: recvied < 0 bytes from multicast: " << strerror(errno)
                          << std::endl;
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                          << "Execution will continue only with FS discovery support" << std::endl;
                return;
            }

            if (std::string(incomingMessage) ==
                std::string(ownHostname) + ":" + std::to_string(backend_port)) {
                // skip myself
                continue;
            }

            std::lock_guard lg(*token_used_to_connect_mutex);
            if (std::find(token_used_to_connect->begin(), token_used_to_connect->end(),
                          incomingMessage) == token_used_to_connect->end()) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                          << "Connecting to  " << incomingMessage << " from multicast advert."
                          << std::endl;
                token_used_to_connect->push_back(incomingMessage);
                capio_backend->connect_to(incomingMessage);
            }

            sleep(1);
        }
    }

    /*
     * Monitor the file system for the presence of tokens.
     */
    static void
    find_new_server_from_fs_token_thread(const bool *continue_execution,
                                         std::vector<std::string> *token_used_to_connect,
                                         std::mutex *token_used_to_connect_mutex) {
        START_LOG(gettid(), "call()");

        if (!continue_execution) {
            LOG("Terminating execution");
            return;
        }

        auto dir_iterator = std::filesystem::directory_iterator(std::filesystem::current_path());
        for (const auto &entry : dir_iterator) {
            const auto token_path = entry.path();

            if (!entry.is_regular_file() || token_path.extension() != ".alive_connection") {
                LOG("Filename %s is not valid", entry.path().c_str());
                continue;
            }

            LOG("Found token %s", token_path.c_str());

            std::ifstream MyReadFile(token_path.filename());
            std::string remoteHost = entry.path().stem(), remotePort;
            LOG("Testing for file: %s (hostname: %s, port=%s)", entry.path().filename().c_str(),
                remoteHost.c_str(), remotePort.c_str());

            getline(MyReadFile, remotePort);
            MyReadFile.close();

            const auto hostname_port = std::string(remoteHost) + ":" + remotePort;
            std::lock_guard lock(*token_used_to_connect_mutex);
            if (std::find(token_used_to_connect->begin(), token_used_to_connect->end(),
                          hostname_port) != token_used_to_connect->end()) {
                LOG("Token already handled... skipping it!");
                continue;
            };

            // TODO: as of now we will not connect with servers
            // TODO: that terminates and then comes back up online...
            token_used_to_connect->push_back(hostname_port);
            capio_backend->connect_to(std::string(remoteHost) + ":" + remotePort);
        }
        LOG("Terminated loop. sleeping one second");
        sleep(1);
    }

  public:
    ~CapioCommunicationService() {
        *continue_execution = false;

        pthread_cancel(thread_server_finder_multicast->native_handle());
        pthread_cancel(thread_server_finder_fs->native_handle());
        thread_server_finder_fs->join();
        thread_server_finder_multicast->join();

        delete_aliveness_token();
        delete capio_backend;
        delete token_used_to_connect_mutex;
    };

    CapioCommunicationService(std::string &backend_name, const int port) {
        START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());
        *continue_execution = true;
        gethostname(ownHostname, HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection", ownHostname);

        token_used_to_connect_mutex = new std::mutex();

        if (backend_name == "MQTT" || backend_name == "MPI") {
            std::cout
                << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                << "Warn: selected backend is not yet officially supported. Setting backend to TCP"
                << std::endl;
            backend_name = "TCP";
        }

        if (backend_name == "TCP" || backend_name == "UCX") {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                      << "Selected backend is: " << backend_name << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                      << "Selected backend port is: " << port << std::endl;
            capio_backend = new MTCL_backend(backend_name, std::to_string(port),
                                             CAPIO_BACKEND_DEFAULT_SLEEP_TIME);
        } else if (backend_name == "FS") {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                      << "Selected backend is File System" << std::endl;
            capio_backend = new NoBackend();
        } else {
            START_LOG(gettid(), "call()");
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "Provided communication backend " << backend_name << " is invalid"
                      << std::endl;
            ERR_EXIT("No valid backend was provided");
        }
        generate_aliveness_token(port);
        thread_server_finder_fs =
            new std::thread(find_new_server_from_fs_token_thread, std::ref(continue_execution),
                            &token_used_to_connect, token_used_to_connect_mutex);

        thread_server_finder_multicast =
            new std::thread(find_new_server_from_multicast_thread, std::ref(continue_execution),
                            &token_used_to_connect, token_used_to_connect_mutex, port);
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_HPP
