#ifndef CAPIO_FS_BACKEND_HPP
#define CAPIO_FS_BACKEND_HPP

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef CAPIO_FS_SOCK_LISTEN_PORT
#define CAPIO_FS_SOCK_LISTEN_PORT 12345
#endif // CAPIO_FS_SOCK_LISTEN_PORT

class FSBackend : public Backend {
  private:
    struct hostent *host_entry = {};
    std::set<std::string> nodes; // store nodes seen from files locations
    std::unordered_map<std::string, std::vector<std::string>>
        hostname_to_ip; // store the nodes to which a token in handshake exists
    std::unordered_map<std::string, std::string>
        ip_to_hostname; // store the nodes to which a token in handshake exists

    // TODO: use parameters to change this
    std::filesystem::path root_dir = "capio_fs", storage_dir = "storage",
                          handshake_dir = "handshake";

    int incomingSocket = -1;

    inline void start_listener_socket() {
        START_LOG(gettid(), "call()");

        struct sockaddr_in serverAddress = {};

        char *IPbuffer = inet_ntoa(*((struct in_addr *) host_entry->h_addr_list[0]));

        LOG("Server IP address: %s", IPbuffer);

        serverAddress.sin_family      = AF_INET;
        serverAddress.sin_port        = CAPIO_FS_SOCK_LISTEN_PORT;
        serverAddress.sin_addr.s_addr = inet_addr(IPbuffer);

        if (((incomingSocket = socket(AF_INET, SOCK_STREAM, 0))) == -1) {
            ERR_EXIT("Error in socket(): error is: %d", incomingSocket);
        }
        LOG("Socket created");

        if (bind(incomingSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) == -1) {
            ERR_EXIT("Error in server socket bind()!");
        }

        LOG("Socket bind");

        if ((listen(incomingSocket, 1)) == -1) {
            ERR_EXIT("Error in server socket listen()");
        }

        LOG("Socket listen");
    }

    inline void create_capio_fs(const std::string &name) {
        std::filesystem::create_directories(root_dir);
        std::filesystem::create_directories(root_dir / storage_dir);
        std::filesystem::create_directories(root_dir / storage_dir / name);
        std::filesystem::create_directories(root_dir / handshake_dir);
    }

    /*
     * Returns true if @param address is in the following range
     * - 10.0.0.0/8
     * - 172.16.0.0/12
     * - 192.168.0.0/16
     */
    static inline bool ip_is_private(uint32_t address) {
        START_LOG(gettid(), "call(address=%ld)", address);
        return (htonl(address) >= 0x0a000000 && htonl(address) <= 0x0affffff) || // 10.0.0.0/8
               (htonl(address) >= 0xac100000 && htonl(address) <= 0xac1fffff) || // 172.16.0.0/12
               (htonl(address) >= 0xc0a80000 && htonl(address) <= 0xc0a8ffff);   // 192.168.0.0/16;
    }
    /*
     * Create a token inside the handshake directory to let people know this instance of
     * capio server exists
     */
    inline bool create_handshake_token(const std::string &name, bool create_exclusive = false) {
        struct ifaddrs *interfaces = nullptr;
        struct in_addr *address;

        START_LOG(gettid(), "call(name=%s, create_exclusive=%s)", name.c_str(),
                  create_exclusive ? "true" : "false");

        std::filesystem::path token_path = root_dir / handshake_dir / std::string(name);

        LOG("Token path computed is: %s", token_path.c_str());

        if (create_exclusive) {
            int fd;
            if ((fd = open(token_path.c_str(), O_EXCL | O_CREAT, 0660)) == -1) {
                LOG("Unable to create token %s with flags O_EXCL | O_CREAT. Maybe another server "
                    "created it...",
                    token_path.c_str());
                return false;
            }
            LOG("Created token %s", token_path.c_str());
            close(fd);
        }

        std::ofstream token(token_path);

        if (getifaddrs(&interfaces) != 0) {
            ERR_EXIT("Unable to obtain network interface description list. aborting now");
        }

        for (struct ifaddrs *entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
            if (entry->ifa_addr->sa_family == AF_INET) {
                if (entry->ifa_addr != nullptr) {
                    char buffer[INET_ADDRSTRLEN] = {};
                    address = &(((struct sockaddr_in *) (entry->ifa_addr))->sin_addr);
                    inet_ntop(entry->ifa_addr->sa_family, address, buffer, INET_ADDRSTRLEN);
                    LOG("Found IP addr: %s", buffer);
                    std::string ip_str(buffer);
                    if (ip_is_private(address->s_addr)) {
                        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "IP address " << buffer
                                  << " (0x" << std::hex << address->s_addr << std::dec
                                  << ") is private. Adding to token file" << std::endl;
                        token << buffer << std::endl;
                        ip_to_hostname.insert({buffer, node_name});
                        hostname_to_ip.at(node_name).emplace_back(buffer);
                    } else {
                        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "IP address " << buffer
                                  << " (0x" << std::hex << address->s_addr << std::dec
                                  << ") is public. ignoring IP" << std::endl;
                    }
                }
            }
        }
        freeifaddrs(interfaces);
        token.close();

        return true;
    }

    inline void read_ip_address_from_token(const std::string &token_name) {
        START_LOG(gettid(), "call(token_name=%s)", token_name.c_str());
        if (token_name == node_name) { // skip self token
            return;
        }
        std::ifstream file(root_dir / handshake_dir / token_name);
        std::string line;
        hostname_to_ip.insert({token_name, {}});
        while (std::getline(file, line)) {
            LOG("Found IP %s for host %s", token_name.c_str(), line.c_str());
            ip_to_hostname.insert({line, token_name});
            hostname_to_ip.at(token_name).emplace_back(line);
        }
    }

  public:
    FSBackend(int argc, char **argv) {
        START_LOG(gettid(), "call(argc=%d)", argc);
        node_name = new char[HOST_NAME_MAX];
        gethostname(node_name, HOST_NAME_MAX);
        host_entry = gethostbyname(node_name);
        hostname_to_ip.insert({node_name, {}}); // create entry to insert ip address

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Hostname is " << node_name << std::endl;

        this->create_capio_fs(node_name);

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Created capio_fs" << std::endl;

        LOG("Backend initialized successfully...");
    };

    ~FSBackend() override {
        START_LOG(gettid(), "call()");
        std::filesystem::remove(root_dir / handshake_dir / std::string(node_name));
        LOG("removed token");
        close(incomingSocket);
    };

    /**
     * Looks for all files locations currently in the capio workflow execution, and return
     * the updated set, as some of them might be new or offline
     * @return
     */
    const std::set<std::string> get_nodes() override {
        START_LOG(gettid(), "call()");
        for (const auto &entry : std::filesystem::directory_iterator(".")) {
            auto found_hostname = entry.path().stem().string().erase(
                0, std::strlen(CAPIO_SERVER_FILES_LOCATION_NAME) - 6); // remove %s.txt from string
            LOG("Found hostname: %s", found_hostname.c_str());
            nodes.insert(found_hostname);
        }
        return nodes;
    };

    // collect information of currently existing tokens in capio
    void handshake_servers() override {
        START_LOG(gettid(), "call()");

        this->create_handshake_token(node_name);
        LOG("Created presence token in capio_fs");
        this->start_listener_socket();
        LOG("backend socket listener initialized");

        for (const auto &entry : std::filesystem::directory_iterator(root_dir / handshake_dir)) {
            auto token = entry.path().stem().string();
            LOG("Found token %s", token.c_str());
            this->read_ip_address_from_token(token);
        }
    };

    RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        struct sockaddr_in clientAddress = {};
        int connectSocket;
        socklen_t clientAddressLen;
        char *clientIP;
        char tmp_buf[CAPIO_REQ_MAX_SIZE];

        if ((connectSocket = accept(incomingSocket, (struct sockaddr *) &clientAddress,
                                    &clientAddressLen)) == -1) {
            close(incomingSocket);
            ERR_EXIT("Error in accept(). Error is %d", connectSocket);
        }

        LOG("Incoming request accepted");

        clientIP = inet_ntoa(clientAddress.sin_addr); // save client ip in string format

        LOG("Incoming request ip: %s", clientIP);

        while (read(connectSocket, tmp_buf, CAPIO_REQ_MAX_SIZE - 1) > 0) {
            LOG("Received request from %s: %s\n", clientIP, tmp_buf);
        }
        tmp_buf[CAPIO_REQ_MAX_SIZE - 1] = '\0';
        close(connectSocket);

        return {tmp_buf, ip_to_hostname.at(tmp_buf)};
    };

    /**
     * Sends a request to target.
     * If capio reacheds this point, then now, or at a certain point in time, a CAPIO server
     * was / is present and online. With this knowloedge, I first need to check whether the token
     * Is present or not. if it is not present, then I shall create it with exlusive permission.
     * If creation fails, then some one else has beaten me in creating the file. I shall then update
     * the IP addresses of the target.
     * If I have managed to create the token, I shall handle the request by myself in
     * place of the dead server
     *
     * @param message
     * @param message_len
     * @param target
     */
    void send_request(const char *message, int message_len, const std::string &target) override {
        START_LOG(gettid(), "call(message=%s, message_len=%d), target=%s)", message, message_len,
                  target.c_str());
        struct sockaddr_in serverAddress {};
        int outgoingSocket;
        auto target_ip = hostname_to_ip[target];

        // check if token is present, if not, proceed to create placeholder token
        if (!std::filesystem::exists(root_dir / handshake_dir / target)) {
            if (!create_handshake_token(target, true)) {
                // lost race to become the dead server. Update target ip information
                this->read_ip_address_from_token(target);
            }
            // else I am answering in place of the dead one
        }

        // at this point I have all the information to proceed to try a connection to target

        if ((outgoingSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            ERR_EXIT("Error in socket(): error is: %d", outgoingSocket);
        }

        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port   = htons(CAPIO_FS_SOCK_LISTEN_PORT);

        // try to connect to target with all IP addresses that I have found
        bool connected = false;
        for (const auto &ip : target_ip) {
            serverAddress.sin_addr.s_addr = inet_addr(ip.c_str());

            connected = connect(outgoingSocket, (struct sockaddr *) &serverAddress,
                                sizeof(serverAddress)) != -1;
            if (connected) {
                break;
            }
        }

        if (!connected) {
            ERR_EXIT("Error: unable to connect to target host. aborting");
        }

        int returnCode = write(outgoingSocket, message, message_len);

        LOG("Sent %d (%u request) bytes on socket %d\n", returnCode, (unsigned) message_len,
            outgoingSocket);
        close(outgoingSocket);
    };

    /**
     * TODO: Warning: as of now, the FnU is implemented only that subsequent
     * file write can be supported. Random writes in different sections of files,
     * as formally defined by FnU policy are not yet supported
     */
    void send_file(char *buffer, long int nbytes, const std::string &target,
                   const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(buffer=%ld, nbytes=%ld, target=%s, file_path=%s)", buffer, nbytes,
                  target.c_str(), file_path.c_str());
        std::ofstream file;
        file.open(root_dir / storage_dir / std::string(node_name) / file_path.string(),
                  std::ios_base::ate);
        file.write(buffer, nbytes);
        file.close();
    };

    void recv_file(char *buffer, const std::string &source, long int bytes_expected,
                   const std::filesystem::path &file_path) override{

    };
};

#endif // CAPIO_FS_BACKEND_HPP