#ifndef CAPIOCOMMUNICATIONSERVICE_HPP
#define CAPIOCOMMUNICATIONSERVICE_HPP

#include "BackendInterface.hpp"
#include "MTCL_backend.hpp"

#include <algorithm>

class CapioCommunicationService {

    char ownHostname[HOST_NAME_MAX] = {0};
    bool *continue_execution        = new bool;
    std::thread *thread_server_finder_fs;

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

        LOG("Removing alive token %s", token_filename.c_str());
        std::filesystem::remove(token_filename);
        LOG("Removed token");
    }

    /*
     * Monitor the file system for the presence of tokens.
     */
    static void find_new_server_from_fs_token_thread(const bool *continue_execution) {
        START_LOG(gettid(), "call()");

        std::vector<std::string> handled_tokens;

        if (!continue_execution) {
            LOG("Terminating execution");
            return;
        }

        auto dir_iterator = std::filesystem::directory_iterator(std::filesystem::current_path());
        for (const auto &entry : dir_iterator) {
            const auto &token_path = entry.path();

            if (!entry.is_regular_file() || token_path.extension() != ".alive_connection") {
                LOG("Filename %s is not valid", entry.path().c_str());
                continue;
            }

            if (std::find(handled_tokens.begin(), handled_tokens.end(), entry.path()) !=
                handled_tokens.end()) {
                LOG("Token already handled... skipping it!");
                continue;
            };

            LOG("Found token %s", token_path.c_str());
            // TODO: as of now we will not connect with servers
            // TODO: that terminates and then comes back up online...
            handled_tokens.push_back(entry.path());

            std::ifstream MyReadFile(token_path.filename());
            std::string remoteHost = entry.path().stem(), remotePort;
            LOG("Testing for file: %s (hostname: %s, port=%s)", entry.path().filename().c_str(),
                remoteHost.c_str(), remotePort.c_str());

            getline(MyReadFile, remotePort);
            MyReadFile.close();

            capio_backend->connect_to(remoteHost, remotePort);
        }
        LOG("Terminated loop. sleeping one second");
        sleep(1);
    }

  public:
    ~CapioCommunicationService() {
        *continue_execution = false;
        thread_server_finder_fs->join();
        delete_aliveness_token();
        delete capio_backend;
    };

    CapioCommunicationService(std::string &backend_name, const int port) {
        START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());
        *continue_execution = true;
        gethostname(ownHostname, HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection", ownHostname);

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
            new std::thread(find_new_server_from_fs_token_thread, std::ref(continue_execution));
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_HPP
