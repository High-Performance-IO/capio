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

    // structure to store file descriptors of opend files
    std::unordered_map<std::string, int> open_files_descriptors;

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
                                  << ") is public. Adding to token file" << std::endl;
                        token << buffer << std::endl;
                        ip_to_hostname.insert({buffer, node_name});
                        hostname_to_ip.at(node_name).emplace_back(buffer);
                    } else {
                        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "IP address " << buffer
                                  << " (0x" << std::hex << address->s_addr << std::dec
                                  << ") is private. ignoring IP" << std::endl;
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

    inline auto generate_capio_path(std::string path) {
        // TODO: use snprintf for efficiency (and better code)
        START_LOG(gettid(), "call(path=%s)", path.c_str());

        auto home_node = get_file_location(path).first;

        if (path.rfind('/', 0) == 0) {
            path.erase(0, 1);
        }

        auto root_path = (root_dir / storage_dir / home_node);
        if (path.rfind(root_path, 0) == 0) {
            LOG("Path starts already from storage root dir");
            return path;
        }

        auto path_len = root_path.native().size() + 1 + path.length() + 1; //\n final
        char _path[path_len];
        snprintf(_path, path_len, "%s/%s", root_path.native().c_str(), path.c_str());

        std::string computed_path(_path);
        LOG("Computed path is: %s", computed_path.c_str());
        return computed_path;
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
    inline const std::set<std::string> get_nodes() override {
        START_LOG(gettid(), "call()");
        for (const auto &entry : std::filesystem::directory_iterator(".")) {
            // remove "file_locations_"
            auto hostname = entry.path().stem().string().erase(0, 15);
            if (!hostname.empty()) {
                LOG("Found hostname: %s", hostname.c_str());
                nodes.insert(hostname);
            }
        }
        return nodes;
    };

    // collect information of currently existing tokens in capio
    inline void handshake_servers() override {
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

        n_servers = get_nodes().size();
    };

    inline RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        LOG("FS backend does not uses request. For correctness, sockets are created, but not used");
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
     * Is present or not. if it is not present, then I shall create it with exclusive permission.
     * If creation fails, then some one else has beaten me in creating the file. I shall then update
     * the IP addresses of the target.
     * If I have managed to create the token, I shall handle the request by myself in
     * place of the dead server
     *
     * @param message
     * @param message_len
     * @param target
     */
    inline void send_request(const char *message, int message_len,
                             const std::string &target) override {
        START_LOG(gettid(), "call(message=%s, message_len=%d), target=%s)", message, message_len,
                  target.c_str());
        LOG("FS backend does not uses requests... skipping");
    };

    /**
     * TODO: Warning: as of now, the FnU is implemented only that subsequent
     * file write can be supported. Random writes in different sections of files,
     * as formally defined by FnU policy are not yet supported
     */
    inline void send_file(char *buffer, long int nbytes, const std::string &target,
                          const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(buffer=%ld, nbytes=%ld, target=%s, file_path=%s)", buffer, nbytes,
                  target.c_str(), file_path.c_str());
        std::ofstream file;
        file.open(root_dir / storage_dir / std::string(node_name) / file_path.string(),
                  std::ios_base::ate);
        file.write(buffer, nbytes);
        file.close();
    };

    inline void recv_file(char *shm, const std::string &source, long int bytes_expected,
                          const std::filesystem::path &file_path) override{

    };

    inline void notify_backend(enum backendActions actions, std::filesystem::path &file_path,
                               char *buffer, size_t offset, size_t buffer_size,
                               bool is_dir) override {
        START_LOG(gettid(), "call(action=%d, path=%s, offset=%ld, buffer_size=%ld, is_dir=%s)",
                  actions, file_path.c_str(), offset, buffer_size, is_dir ? "true" : "false");

        std::filesystem::path path = generate_capio_path(file_path);

        switch (actions) {
        case createFile: {
            LOG("Creating file %s", path.c_str());
            if (is_dir) {
                std::filesystem::create_directories(path);
            } else {
                int file = open(path.c_str(), O_RDWR | O_CREAT, 0640);
                if (file == -1) {
                    ERR_EXIT("Error creating file. errno is %s", strerror(errno));
                }
                open_files_descriptors.emplace(path, file);
            }
            break;
        }

        case readFile: {
            LOG("Reading %ld bytes from offset %ld of file %s", buffer_size, offset, path.c_str());

            if (file_path == get_capio_dir()) {
                LOG("Returning contents of files that are local to node");
                file_path = (root_dir / storage_dir / node_name);
                path      = file_path;
            }

            if (is_dir) {
                auto dirp = opendir(path.c_str());
                dirent *directory{};
                uint64_t totalSize = 0;
                while ((directory = readdir(dirp)) != nullptr && totalSize < buffer_size) {
                    memcpy(buffer + totalSize, directory, sizeof(dirent));
                    totalSize += sizeof(dirent);
                }

            } else {
                FILE *f = fdopen(open_files_descriptors.at(path), "r");

                // compute file size
                fseek(f, 0L, SEEK_END);
                long int actual_size = ftell(f);
                LOG("File actual size is %ld", actual_size);
                buffer_size = buffer_size > actual_size ? actual_size : buffer_size;
                rewind(f);
                LOG("Will read %ld bytes from file", buffer_size);

                if (f == nullptr) {
                    ERR_EXIT("Error opening file. error is: %s", strerror(errno));
                }
                fseek(f, offset, SEEK_CUR);
                auto read_return = fread(buffer, sizeof(char), buffer_size, f);
                if (!read_return) {
                    ERR_EXIT("Error while fred. errno is %s", strerror(errno));
                }
            }
            break;
        }
        case writeFile: {
            LOG("Writing buffer conttent to FS");
            if (is_dir) {
                LOG("File is directory. skipping as FS is being used");
                return;
            }

            FILE *f = fdopen(open_files_descriptors.at(path), "w");
            fseek(f, offset, SEEK_SET);
            fwrite(buffer, sizeof(char), buffer_size, f);
            fflush(f);
            break;
        }

        case closeFile: {
            close(open_files_descriptors.at(path));
            break;
        }

        case seekFile: {
            *reinterpret_cast<off64_t *>(buffer) =
                fseek(fdopen(open_files_descriptors.at(path), "r"), offset, SEEK_SET);
            break;
        }

        case deleteFile: {
            if (open_files_descriptors.find(path) != open_files_descriptors.end()) {
                close(open_files_descriptors.at(path));
                open_files_descriptors.erase(path);
            }
            unlink(path.c_str());
        }

        default:
            LOG("Error: action not understood!");
        }
    };

    inline bool store_file_in_memory() override { return false; };
};

#endif // CAPIO_FS_BACKEND_HPP