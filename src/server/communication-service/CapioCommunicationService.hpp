#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include "capio/logger.hpp" //se lo tongo non vann i log
#include <capio/semaphore.hpp>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <list>
#include <mtcl.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class CapioCommunicationService : BackendInterface {

  private:
    std::unordered_map<std::string, MTCL::HandleUser> connected_hostnames_map;
    std::string ownHostname, selfToken, connectedHostname;
    MTCL::HandleUser Handler{};
    static MTCL::HandleUser StaticHandler;

    std::thread *th;
    bool *continue_execution         = new bool;
    MTCL::HandleUser *HandlerPointer = new MTCL::HandleUser;

    std::mutex *_guard;

    void static waitConnect(const bool *continue_execution, int sleep_time,
                            std::unordered_map<std::string, MTCL::HandleUser> *open_connections,
                            std::mutex *guard) {

        START_LOG(gettid(), "call(sleep_time=%d)", sleep_time);

        while (*continue_execution) {
            auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));

            if (!UserManager.isValid()) {
                continue;
            }
            LOG("Handle user is valid");

            std::string connected_hostname;
            connected_hostname.reserve(HOST_NAME_MAX);
            UserManager.receive(connected_hostname.data(), HOST_NAME_MAX);

            LOG("Recived connection hostname: %s", connected_hostname.c_str());

            std::lock_guard lock(*guard);
            open_connections->insert({connected_hostname, std::move(UserManager)});
        }
    }
    void generate_aliveness_token(std::string port) const {
        START_LOG(gettid(), "call(port=%s)", port.c_str());

        std::string token_filename(ownHostname.c_str());
        token_filename += ".alive_connection";

        LOG("Creating alive token %s", token_filename.c_str());

        std::ofstream FilePort(token_filename);
        FilePort << port;
        FilePort.close();

        LOG("Saved self token info to FS");
    }

  public:
    explicit CapioCommunicationService(std::string proto, std::string port)
        : selfToken(proto + ":0.0.0.0:" + port) {

        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");

        _guard = new std::mutex();

        ownHostname.reserve(HOST_NAME_MAX);
        gethostname(ownHostname.data(), HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection %s", ownHostname.c_str(),
            selfToken.c_str());

        generate_aliveness_token(port);

        // TODO: check folder against metadata folder....
        auto dir_iterator = std::filesystem::directory_iterator(std::filesystem::current_path());
        for (const auto &entry : dir_iterator) {

            auto token_path = entry.path();

            if (!entry.is_regular_file() || token_path.extension() != ".alive_connection") {
                continue;
            }
            LOG("Found token %s", token_path.c_str());

            std::ifstream MyReadFile(token_path.filename());
            std::string remoteHost = entry.path().stem(), remotePort;
            LOG("Testing for file: %s (token: %s, tryHostName=%s)", entry.path().filename().c_str(),
                selfToken.c_str(), remoteHost.c_str());

            getline(MyReadFile, remotePort);
            MyReadFile.close();

#ifndef CAPIO_BUILD_TESTS
            // Allow connections on loopback interface only when running tests
            if (remoteHost != ownHostname) {
#endif
                std::string remoteToken = proto + ":" + remoteHost + ":" + port;
                LOG("Trying to connect on remote: %s", remoteToken.c_str());

                MTCL::HandleUser UserManager = MTCL::Manager::connect(remoteToken);
                if (UserManager.isValid()) {
                    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Connected to " << remoteToken
                              << std::endl;
                    LOG("Connected to: %s", remoteToken.c_str());
                    std::lock_guard lg(*_guard);
                    connected_hostnames_map.insert({remoteHost, std::move(UserManager)});

                } else {
                    std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "Warning: found token "
                              << token_path.filename() << ", but connection is not valid"
                              << std::endl;
                }

#ifndef CAPIO_BUILD_TESTS
                // Allow connections on loopback interface only when running tests
            }
#endif
        }

        *continue_execution = true;

        MTCL::Manager::init(selfToken);
        MTCL::Manager::listen(selfToken);

        th = new std::thread(waitConnect, std::ref(continue_execution), 30,
                             &connected_hostnames_map, _guard);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;

        // std::cout << "handler" << std::endl;
        Handler = std::move(*HandlerPointer);

        // Handler =ret.get_future().get();
        std::cout << "fine main" << std::endl;
    }

    ~CapioCommunicationService() {
        START_LOG(gettid(), "END");

        // Set the flag to stop the connection thread
        *continue_execution = false;

        while (!th->joinable()) {
            // cicla in loop finche th non e' joinabe
        }
        th->join();
        delete th;

        Handler.close();
        LOG("Handler closed.");

        std::string path = std::filesystem::current_path();
        for (const auto &entry : std::filesystem::directory_iterator(path)) {
            if (entry.path().extension() == ".txt" && (entry.path().stem() != "CMakeLists") &&
                (entry.path().stem() != "CMakeCache")) {
                std::remove(entry.path().filename().c_str());
            }
        }

        MTCL::Manager::finalize();
        LOG("Finalizing MTCL backend");

        /*
        START_LOG(gettid(), "END");

        Handler.close();
        LOG("Finalized MTCL backend");
        MTCL::Manager::finalize();
        LOG("Finalized MTCL backend");
        delete[] ownHostname;

        LOG("Finalized MTCL backend");

        std::string path = std::filesystem::current_path();
        for (const auto &entry : std::filesystem::directory_iterator(path)) {

            if (entry.path().extension() == ".txt" && (entry.path().stem() != "CMakeLists")) {
                std::remove(entry.path().filename().c_str());
            }
        }

        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;*/
    }

    std::string &recive(char *buf, uint64_t buf_size) {
        // while  Handler.probe;
        std::cout << "sono " << ownHostname << " e sono connesso con " << connectedHostname
                  << std::endl;
        START_LOG(gettid(), "inizio recv");
        LOG("Connected to %s", connectedHostname.c_str());
        if (Handler.receive(buf, buf_size) != buf_size) {
            std::cout << "errore!!" << std::endl;
            LOG("errore recv");
            MTCL_ERROR(ownHostname.c_str(), "ERROR receiving message\n");
        }
        LOG("SALTO TIME");
        /*auto endChrono            = std::chrono::system_clock::now(); // catch time
        const std::time_t endTime = std::chrono::system_clock::to_time_t(endChrono);

        void *PointerEndTime = (void *) &endTime;
        Handler.send(PointerEndTime, sizeof(endTime));*/ // send time of recived message

        return connectedHostname;
    }

    /**
     *
     * @param target
     * @param buffer
     * @param buffer_size
     * @param offset
     */
    void send(const std::string &target, char *buf, uint64_t buf_size) {
        // overdrive non serve
        std::cout << "sono " << ownHostname << " e sono connesso con " << target << "\n";

        auto startChrono            = std::chrono::system_clock::now(); // iniza timer
        const std::time_t startTime = std::chrono::system_clock::to_time_t(startChrono);
        // sleep(2);
        std::time_t *TimeEnd        = new std::time_t[1];
        if (target.compare(connectedHostname) == 0) {
            if (Handler.send(buf, buf_size) != buf_size) {
                MTCL_ERROR(ownHostname.c_str(), "ERROR sending message\n");
            } else {
                std::cout << "ho mandato: " << buf << "\n";

                Handler.receive(TimeEnd, sizeof(TimeEnd));     // rimane in attesta del tempo
                std::time_t duration = (*TimeEnd) - startTime; // tempo in secondi
                std::cout << "Il messaggio ci ha messo " << duration << " secondi \n";
                if (duration != 0) {
                    std::cout << "Hai una banda di: " << buf_size / (duration) << " Byte/s \n";
                } else {
                    std::cout << "Hai una banda altissima  \n";
                }
            }
        } else {
            std::cout << "host name non connnesso \n";
        }
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_H
