#include <algorithm>
#include <capio/logger.hpp>
#include <filesystem>
#include <thread>
#include <include/communication-service/control-plane/fs_control_plane.hpp>
#include <include/communication-service/data-plane/backend_interface.hpp>
#include <utils/configuration.hpp>

    void FSControlPlane::generate_aliveness_token(const int port) const {
        START_LOG(gettid(), "call(port=d)", port);

        std::string token_filename(ownHostname);
        token_filename += ".alive_connection";

        LOG("Creating alive token %s", token_filename.c_str());

        std::ofstream FilePort(token_filename);
        FilePort << port;
        FilePort.close();

        LOG("Saved self token info to FS");
        server_println(CAPIO_SERVER_CLI_LOG_SERVER, "Generated token at " + token_filename);
    }

    void FSControlPlane::delete_aliveness_token() {
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

void FSControlPlane::fs_server_aliveness_detector_thread(const bool *continue_execution,
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


    FSControlPlane::FSControlPlane(int backend_port) : _backend_port(backend_port) {
        gethostname(ownHostname, HOST_NAME_MAX);
        generate_aliveness_token(backend_port);
        continue_execution          = new bool(true);
        token_used_to_connect_mutex = new std::mutex();
        thread = new std::thread(fs_server_aliveness_detector_thread, std::ref(continue_execution),
                                 &token_used_to_connect, token_used_to_connect_mutex);
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "FSControlPlane initialization completed.");
    };

    FSControlPlane::~FSControlPlane() {
        delete_aliveness_token();
        pthread_cancel(thread->native_handle());
        thread->join();
        delete thread;
        delete continue_execution;
        delete token_used_to_connect_mutex;

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "FSControlPlane cleanup completed.");
    }

    void FSControlPlane::notify_all(FSControlPlane::event_type event, const std::filesystem::path &path) {}
