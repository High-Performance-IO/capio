#ifndef CAPIO_FS_BACKEND_HPP
#define CAPIO_FS_BACKEND_HPP

#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>
#include <utils/distributed_semaphore.hpp>

class FSBackend : public Backend {
  private:
    std::set<std::string> nodes; // store nodes seen from files locations

    // TODO: use parameters to change this
    std::filesystem::path root_dir = "capio_fs", storage_dir = "storage",
                          handshake_dir = "handshake", comm_pipe = "comms";

    // structure to store file descriptors of opend files
    std::unordered_map<std::string, int> open_files_descriptors;

    // FD to file on which recive requests
    int selfCommLinkFile = -1;

    inline void create_capio_fs(const std::string &name) {
        std::filesystem::create_directories(root_dir);
        std::filesystem::create_directories(root_dir / storage_dir);
        std::filesystem::create_directories(root_dir / storage_dir / name);
        std::filesystem::create_directories(root_dir / handshake_dir);
        std::filesystem::create_directories(root_dir / comm_pipe);
    }

    /*
     * Create a token inside the handshake directory to let people know this instance of
     * capio server exists
     */
    inline bool create_handshake_token(const std::string &name, bool create_exclusive = false) {

        START_LOG(gettid(), "call(name=%s, create_exclusive=%s)", name.c_str(),
                  create_exclusive ? "true" : "false");

        std::filesystem::path token_path = root_dir / handshake_dir / std::string(name);

        LOG("Token path computed is: %s", token_path.c_str());

        int fd;
        if ((fd = open(token_path.c_str(), create_exclusive ? O_EXCL | O_CREAT : O_CREAT, 0660)) ==
            -1) {
            LOG("Unable to create token %s with flags %s. Maybe another server created it...",
                token_path.c_str(), create_exclusive ? "O_EXCL | O_CREAT" : "O_CREAT");
            return false;
        }
        LOG("Created token %s", token_path.c_str());
        close(fd);

        // create file for current node requests
        if ((selfCommLinkFile = open((root_dir / comm_pipe / node_name).c_str(),
                                     O_EXCL | O_CREAT | O_RDWR, 0660)) == -1) {

            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                      << "Unable to create communication pipe." << std::endl;
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                      << "as it might already be present. Deleting and retrying." << std::endl;

            std::filesystem::remove(root_dir / comm_pipe / node_name);
            if ((selfCommLinkFile = open((root_dir / comm_pipe / node_name).c_str(),
                                         O_EXCL | O_CREAT | O_RDWR, 0660)) == -1) {
                std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << " [ " << node_name << " ] "
                          << "Unable to create communication pipe" << std::endl;
                ERR_EXIT("Unable to create named pipe for incoming communications. Error is %s",
                         strerror(errno));
            }
        }
        LOG("Incoming communication Named Pipe created successfully");

        return true;
    }

    inline auto generate_capio_path(std::string path, Backend::backendActions action) {
        // TODO: use snprintf for efficiency (and better code)
        START_LOG(gettid(), "call(path=%s)", path.c_str());

        std::string home_node;

        if (path == get_capio_dir() || action == Backend::backendActions::createFile) {
            home_node = node_name;
        } else {
            auto loc = get_file_location_opt(path);
            LOG("Searching for file with path=%s, on node %s", path.c_str(), loc->get().first);
            home_node = loc->get().first;
        }
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

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Hostname is " << node_name << std::endl;

        this->create_capio_fs(node_name);

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Created capio_fs" << std::endl;

        this->create_handshake_token(node_name);
        LOG("Created presence token in capio_fs");

        LOG("Backend initialized successfully...");
    };

    ~FSBackend() override {
        START_LOG(gettid(), "call()");
        if (std::filesystem::remove(root_dir / handshake_dir / node_name)) {
            LOG("removed token");
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                      << "Removed presence token" << std::endl;
        } else {
            LOG("unable to remove token");
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << " [ " << node_name << " ] "
                      << "Unable to remove presence token" << std::endl;
        }

        if (std::filesystem::remove(root_dir / comm_pipe / node_name)) {
            LOG("Communication pipe removed successfully");
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                      << "Removed communication pipe" << std::endl;
        } else {
            LOG("Unable to remove communication pipe");
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << " [ " << node_name << " ] "
                      << "Unable to remove communication pipe" << std::endl;
        }
    };

    /**
     * Looks for all files locations currently in the capio workflow execution, and return
     * the updated set, as some of them might be new or offline
     * @return
     */
    inline const std::set<std::string> get_nodes() override {
        START_LOG(gettid(), "call()");
        const std::regex file_location_regex{"(files_location_)+(.*)+"};
        for (const auto &entry : std::filesystem::directory_iterator(".")) {
            // remove "files_location_"
            std::string pathname = entry.path().stem().string();
            if (std::regex_match(pathname, file_location_regex)) {
                auto hostname = pathname.erase(0, strlen("files_location_"));
                LOG("Found hostname: %s", hostname.c_str());
                nodes.insert(hostname);
            }
        }
        return nodes;
    };

    // collect information of currently existing tokens in capio
    inline void handshake_servers() override {
        START_LOG(gettid(), "call()");

        for (const auto &entry : std::filesystem::directory_iterator(root_dir / handshake_dir)) {
            LOG("Found token %s", entry.path().stem().c_str());
        }

        n_servers = get_nodes().size();
    };

    inline RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        ssize_t readValue = 0;
        char *message     = new char[HOST_NAME_MAX + PATH_MAX + PATH_MAX]{};

        // keep reading until data arrives. If 0 is provided, fifo has been closed on other side
        while (readValue <= 0) {
            readValue = read(selfCommLinkFile, message, HOST_NAME_MAX + PATH_MAX + PATH_MAX);
            if (readValue == -1 && errno != EINTR) {
                ERR_EXIT("Error reading from incoming fifo queue: errno is %s", strerror(errno));
            } else if (readValue == -1) {
                LOG("Warning: readline returned -1 with error code: %s", strerror(errno));
            }
        }
        LOG("Recived <%s> on communication link.", message);

        const char *src = strtok(message, "@");
        const char *msg = strtok(nullptr, "@");

        return {msg, src};
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

        int targetNodeFile         = -1;
        const std::string lockFile = (root_dir / comm_pipe / target).string() + ".lock";

        std::string line = target + "@" + message + "\n";

        // TODO: handle dead nodes

        // Exclusive lock on pipe
        DistributedSemaphore locking(lockFile, 100);

        LOG("Acquired lock. preparing to send data on pipe %s",
            (root_dir / comm_pipe / target).c_str());

        // open target node file
        if ((targetNodeFile = open((root_dir / comm_pipe / target).c_str(), O_RDWR | O_APPEND)) <
            0) {
            ERR_EXIT("Unable to open pipe: errno is %s", strerror(errno));
        }
        LOG("Successfully opend pipe %s", (root_dir / comm_pipe / target).c_str());
        // send data

        if (write(targetNodeFile, line.c_str(), line.length()) == -1) {
            ERR_EXIT("Error: unable to send source node name to target node. errno is %s",
                     strerror(errno));
        }

        LOG("Request <%s> has been sent", line.c_str());
        // cleanup and unlock
        close(targetNodeFile);
        LOG("Closed target pipe");
    };

    /**
     * TODO: Warning: as of now, the FnU is implemented only that subsequent
     * file write can be supported. Random writes in different sections of files,
     * as formally defined by FnU policy are not yet supported
     */
    inline void send_file(char *buffer, long int nbytes, long int offset, const std::string &target,
                          const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(buffer=%ld, nbytes=%ld, target=%s, file_path=%s)", buffer, nbytes,
                  target.c_str(), file_path.c_str());
        std::ofstream file;
        file.seekp(offset);
        file.open(root_dir / storage_dir / std::string(node_name) / file_path.string(),
                  std::ios_base::ate);
        file.write(buffer, nbytes);
        file.close();
    };

    /**
     * TODO: Warning: as of now, the FnU is implemented only that subsequent
     * file write can be supported. Random writes in different sections of files,
     * as formally defined by FnU policy are not yet supported
     */
    inline void recv_file(char *shm, const std::string &source, long int bytes_expected,
                          long int offset, const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call(source=%s, bytes_expected=%ld, file_path=%s)", source.c_str(),
                  bytes_expected, file_path.c_str());
        std::ifstream file;
        file.open(root_dir / storage_dir / std::string(node_name) / file_path.string(),
                  std::ios_base::in);
        file.seekg(offset);
        file.read(shm, bytes_expected);
        file.close();
    };

    inline void notify_backend(enum backendActions actions, std::filesystem::path &file_path,
                               char *buffer, size_t offset, size_t buffer_size,
                               bool is_dir) override {
        START_LOG(gettid(), "call(action=%d, path=%s, offset=%ld, buffer_size=%ld, is_dir=%s)",
                  actions, file_path.c_str(), offset, buffer_size, is_dir ? "true" : "false");

        std::filesystem::path path = generate_capio_path(file_path, actions);

        switch (actions) {
        case createFile: {
            LOG("Creating file %s", path.c_str());
            if (is_dir) {
                std::filesystem::create_directories(path);
            } else {
                std::filesystem::path parent_dir = path;
                std::filesystem::create_directories(parent_dir.remove_filename());
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