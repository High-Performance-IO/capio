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
#include <queue>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __CAPIO_BUILD_TESTS
#define ALLOW_CONNECT_TO_SELF true
#else
#define ALLOW_CONNECT_TO_SELF false
#endif

class TransportUnit {
  public:
    char target[HOST_NAME_MAX];
    char source[HOST_NAME_MAX];
    char filepath[PATH_MAX];
    char *bytes;
    capio_off64_t buffer_size;
    capio_off64_t start_write_offset;
};

class CapioCommunicationService : BackendInterface {
    typedef std::tuple<std::queue<TransportUnit> *, std::queue<TransportUnit> *, std::mutex *>
        TransportUnitInterface;

    std::unordered_map<std::string, TransportUnitInterface> connected_hostnames_map;
    std::string selfToken, connectedHostname, ownPort;
    char ownHostname[HOST_NAME_MAX] = {0};
    static MTCL::HandleUser StaticHandler;
    int thread_sleep_times = 0;
    std::thread *th;
    bool *continue_execution         = new bool;
    MTCL::HandleUser *HandlerPointer = new MTCL::HandleUser;
    std::mutex *_guard;

    std::vector<std::thread *> connection_threads;

    /**
     * This thread will handle connections towards a single target.
     * @param HandlerPointer
     * @param interface
     */
    void static connect_thread_handler(MTCL::HandleUser HandlerPointer, const char *remote_hostname,
                                       const int sleep_time, TransportUnitInterface interface) {
        START_LOG(gettid(), "call(remote_hostname=%s)", remote_hostname);
        // out = data to sent to others
        // in = data from others
        auto [in, out, mutex] = interface;

        while (HandlerPointer.isValid()) {
            std::lock_guard lg(*mutex);

            while (out->size() > 0) {
                auto unit = out->front();
                out->pop();
                /**
                 * step0: send recive buffer size
                 * step1: send offset of write
                 * step2: send data
                 */
                HandlerPointer.send(&unit.buffer_size, sizeof(capio_off64_t));
                HandlerPointer.send(&unit.start_write_offset, sizeof(capio_off64_t));
                HandlerPointer.send(unit.bytes, unit.buffer_size);
            }

            // todo: check if loop is required
            size_t recive_size = 0;
            HandlerPointer.probe(recive_size, false);
            if (recive_size > 0) {
                TransportUnit unit;
                HandlerPointer.receive(&unit.buffer_size, sizeof(capio_off64_t));
                HandlerPointer.receive(&unit.start_write_offset, sizeof(capio_off64_t));
                unit.bytes = new char[unit.buffer_size];
                HandlerPointer.receive(unit.bytes, unit.buffer_size);

                out->push(unit);
            }
        }
    }

    void static waitConnect(
        const bool *continue_execution, int sleep_time,
        std::unordered_map<std::string, TransportUnitInterface> *open_connections,
        std::mutex *guard, std::vector<std::thread *> *_connection_threads) {

        START_LOG(gettid(), "call(sleep_time=%d)", sleep_time);

        while (*continue_execution) {
            auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));

            if (!UserManager.isValid()) {
                continue;
            }
            LOG("Handle user is valid");
            char connected_hostname[HOST_NAME_MAX] = {0};
            UserManager.receive(connected_hostname, HOST_NAME_MAX);

            std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                      << "Connected to " << connected_hostname << std::endl;

            LOG("Recived connection hostname: %s", connected_hostname);

            std::lock_guard lock(*guard);

            open_connections->insert(
                {connected_hostname,
                 std::make_tuple(new std::queue<TransportUnit>(), new std::queue<TransportUnit>(),
                                 new std::mutex())});

            _connection_threads->push_back(
                new std::thread(connect_thread_handler, std::move(UserManager), connected_hostname,
                                sleep_time, open_connections->at(connected_hostname)));
        }
    }

    void generate_aliveness_token(std::string port) const {
        START_LOG(gettid(), "call(port=%s)", port.c_str());

        std::string token_filename(ownHostname);
        token_filename += ".alive_connection";

        LOG("Creating alive token %s", token_filename.c_str());

        std::ofstream FilePort(token_filename);
        FilePort << port;
        FilePort.close();

        LOG("Saved self token info to FS");
    }

    void delete_aliveness_token(const std::string &port) const {
        START_LOG(gettid(), "call(port=%s)", port.c_str());

        std::string token_filename(ownHostname);
        token_filename += ".alive_connection";

        LOG("Removing alive token %s", token_filename.c_str());
        std::filesystem::remove(token_filename);
        LOG("Removed token");
    }

  public:
    explicit CapioCommunicationService(std::string proto, std::string port, int sleep_time)
        : selfToken(proto + ":0.0.0.0:" + port), ownPort(port), thread_sleep_times(sleep_time) {

        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");

        _guard = new std::mutex();

        if (ALLOW_CONNECT_TO_SELF) {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                      << "Warning: CAPIO has been build with tests: connections on loopback "
                         "interfaces will be allowed";
        }

        gethostname(ownHostname, HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection %s", ownHostname,
            selfToken.c_str());

        std::string hostname_id("server-");
        hostname_id += ownHostname;
        MTCL::Manager::init(hostname_id);

        generate_aliveness_token(ownPort);

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

            std::string remoteToken = proto + ":" + remoteHost + ":" + ownPort;
            LOG("Trying to connect on remote: %s", remoteToken.c_str());

            MTCL::HandleUser UserManager = MTCL::Manager::connect(remoteToken);
            if (UserManager.isValid()) {
                std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                          << "Connected to " << remoteToken << std::endl;
                LOG("Connected to: %s", remoteToken.c_str());
                UserManager.send(ownHostname, HOST_NAME_MAX);
                std::lock_guard lg(*_guard);

                auto connection_tuple =
                    std::make_tuple(new std::queue<TransportUnit>(),
                                    new std::queue<TransportUnit>(), new std::mutex());
                connected_hostnames_map.insert({remoteHost, connection_tuple});

                connection_threads.push_back(new std::thread(
                    connect_thread_handler, std::move(UserManager), remoteHost.c_str(), sleep_time, connection_tuple));

            } else {
                std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                          << "Warning: found token " << token_path.filename()
                          << ", but connection is not valid" << std::endl;
            }
        }

        *continue_execution = true;

        MTCL::Manager::listen(selfToken);

        th = new std::thread(waitConnect, std::ref(continue_execution), 30,
                             &connected_hostnames_map, _guard, &connection_threads);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;
    }

    ~CapioCommunicationService() {

        START_LOG(gettid(), "call()");

        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;

        LOG("Handler closed.");

        delete_aliveness_token(ownPort);

        MTCL::Manager::finalize();
        LOG("Finalizing MTCL backend");
    }

    std::string &recive(char *buf, uint64_t buf_size) {/*
        // while  Handler.probe;
        std::cout << "sono " << ownHostname << " e sono connesso con " << connectedHostname
                  << std::endl;
        START_LOG(gettid(), "inizio recv");
        LOG("Connected to %s", connectedHostname.c_str());
        if (Handler.receive(buf, buf_size) != buf_size) {
            std::cout << "errore!!" << std::endl;
            LOG("errore recv");
            MTCL_ERROR(ownHostname, "ERROR receiving message\n");
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
        /*   std::cout << "sono " << ownHostname << " e sono connesso con " << target << "\n";

           auto startChrono            = std::chrono::system_clock::now(); // iniza timer
           const std::time_t startTime = std::chrono::system_clock::to_time_t(startChrono);
           // sleep(2);
           std::time_t *TimeEnd        = new std::time_t[1];
           if (target.compare(connectedHostname) == 0) {
               if (Handler.send(buf, buf_size) != buf_size) {
                   MTCL_ERROR(ownHostname, "ERROR sending message\n");
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
           }*/
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_H
