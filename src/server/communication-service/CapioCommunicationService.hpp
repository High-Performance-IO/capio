#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include <capio/logger.hpp>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mtcl.hpp>
#include <netinet/in.h>
#include <queue>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class TransportUnit {
  protected:
    std::string _filepath;
    char *_bytes;
    capio_off64_t _buffer_size;
    capio_off64_t _start_write_offset;

  public:
    TransportUnit() = default;

    ~TransportUnit() { delete[] _bytes; }

    friend class CapioCommunicationService;
};

class CapioCommunicationService : BackendInterface {
    typedef std::tuple<std::queue<TransportUnit> *, std::queue<TransportUnit> *, std::mutex *>
        TransportUnitInterface;
    std::unordered_map<std::string, TransportUnitInterface> connected_hostnames_map;
    std::string selfToken, connectedHostname, ownPort;
    char ownHostname[HOST_NAME_MAX] = {0};
    int thread_sleep_times          = 0;
    bool *continue_execution        = new bool;
    std::mutex *_guard;
    std::thread *th;
    std::vector<std::thread *> connection_threads; // utile solo per distruttore?

    /**
     * This thread will handle connections towards a single target.
     */
    void static server_connection_handler(MTCL::HandleUser HandlerPointer,
                                          const char *remote_hostname, const int sleep_time,
                                          TransportUnitInterface interface) {
        START_LOG(gettid(), "call(remote_hostname=%s)", remote_hostname);
        // out = data to sent to others
        // in = data from others
        auto [in, out, mutex] = interface;

        while (HandlerPointer.isValid()) {
            // finche' la connesisone e' stabile
            // execute up to N operation of send &/or recive, to avoid starvation due to semaphores.
            constexpr int max_net_op = 10;

            // Send phase
            LOG("Starting send section");
            for (int completed_io_operations = 0;
                 completed_io_operations < max_net_op && !out->empty(); ++completed_io_operations) {
                std::lock_guard lg(*mutex);
                LOG("Locked guard");
                auto &unit = out->front();

                LOG("Sending %ld bytes of file %s to %s", unit._buffer_size, unit._filepath.c_str(),
                    remote_hostname);
                /**
                 * step0: send file path
                 * step1: send recive buffer size
                 * step1: send offset of write
                 * step2: send data
                 */
                HandlerPointer.send(unit._filepath.c_str(), PATH_MAX);
                HandlerPointer.send(&unit._buffer_size, sizeof(capio_off64_t));
                HandlerPointer.send(unit._bytes, unit._buffer_size);
                HandlerPointer.send(&unit._start_write_offset, sizeof(capio_off64_t));
                LOG("Message sent");

                out->pop();
            }

            LOG("Starting receive section");
            // Recive phase
            size_t recive_size = 0;
            HandlerPointer.probe(recive_size, false);
            for (int completed_io_operations = 0;
                 completed_io_operations < max_net_op && recive_size > 0;
                 ++completed_io_operations, HandlerPointer.probe(recive_size, false)) {
                LOG("Lock guard");
                std::lock_guard lg(*mutex);
                LOG("Receiving data");
                TransportUnit unit;
                unit._filepath.reserve(PATH_MAX + 1);
                HandlerPointer.receive(unit._filepath.data(), PATH_MAX);
                HandlerPointer.receive(&unit._buffer_size, sizeof(capio_off64_t));
                LOG("Receiving %ld bytes of file %s", unit._buffer_size, unit._filepath.c_str());
                unit._bytes = new char[unit._buffer_size];
                HandlerPointer.receive(unit._bytes, PATH_MAX);
                std::cout << unit._bytes << std::endl;
                HandlerPointer.receive(&unit._start_write_offset, sizeof(capio_off64_t));
                LOG("Offset of recived chunk is %ld", unit._start_write_offset);
                in->push(unit);
                LOG("Pushed %ld bytes to be stored on file %s", unit._buffer_size,
                    unit._filepath.c_str());

                completed_io_operations++;
                HandlerPointer.probe(recive_size, false);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }
    // controllo pop non ditrugge elemeneto
    //  pop alla fine
    void static incoming_connection_listener(
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

            _connection_threads->push_back(new std::thread(
                server_connection_handler, std::move(UserManager), connected_hostname, sleep_time,
                open_connections->at(connected_hostname)));
        }
    }

    void generate_aliveness_token(const std::string &port) const {
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

        gethostname(ownHostname, HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection %s", ownHostname,
            selfToken.c_str());

        std::string hostname_id("server-");
        hostname_id += ownHostname;
        MTCL::Manager::init(hostname_id);

        generate_aliveness_token(ownPort);

        auto dir_iterator = std::filesystem::directory_iterator(std::filesystem::current_path());
        for (const auto &entry : dir_iterator) {
            const auto &token_path = entry.path();

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

                connection_threads.push_back(
                    new std::thread(server_connection_handler, std::move(UserManager),
                                    remoteHost.c_str(), sleep_time, connection_tuple));
            } else {
                std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                          << "Warning: found token " << token_path.filename()
                          << ", but connection is not valid" << std::endl;
            }
        }

        *continue_execution = true;

        MTCL::Manager::listen(selfToken);

        th = new std::thread(incoming_connection_listener, std::ref(continue_execution), sleep_time,
                             &connected_hostnames_map, _guard, &connection_threads);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;
    }

    ~CapioCommunicationService() override {
        START_LOG(gettid(), "call()");

        for (auto thread : connection_threads) {
            if (!thread->joinable()) {
                pthread_cancel(thread->native_handle());
            }
            thread->join();
        }
        LOG("Terminated connection threads");

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

    std::string &recive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) override {
        START_LOG(gettid(), "call()");

        std::queue<TransportUnit> *inQueue = nullptr;
        TransportUnitInterface i;
        bool found = false;
        while (!found) {
            for (auto [hostname, data] : connected_hostnames_map) {
                auto *queue = std::get<0>(data);

                if (found = !queue->empty()) {
                    LOG("Found data received from %s", hostname.c_str());
                    inQueue = queue;
                }
            }
            if (!found) {
                LOG("No incoming messages. Putting thread to sleep");
                std::this_thread::sleep_for(std::chrono::milliseconds(thread_sleep_times));
            }
        }

        std::lock_guard lg(*std::get<2>(i));
        TransportUnit &TopQueue = inQueue->front();
        *buf_size               = TopQueue._buffer_size;
        *start_offset           = TopQueue._start_write_offset;
        memcpy(buf, TopQueue._bytes, *buf_size);

        inQueue->pop();
        return TopQueue._filepath;
    }

    void send(const std::string &target, char *buf, uint64_t buf_size, const std::string &filepath,
              const capio_off64_t start_offset) override {
        START_LOG(gettid(), "Out queue upload");
        std::cout << "send queue out" << std::endl;
        if (auto element = connected_hostnames_map.find(target);
            element != connected_hostnames_map.end()) {
            TransportUnitInterface i = element->second;
            LOG("found target");
            std::cout << "found target" << std::endl;
            std::queue<TransportUnit> *out = std::get<1>(i);
            std::lock_guard lg(*std::get<2>(i));
            TransportUnit TrasportOut;
            TrasportOut._buffer_size = buf_size;
            TrasportOut._bytes       = new char[buf_size];
            memcpy(TrasportOut._bytes, buf, buf_size);
            TrasportOut._filepath           = filepath;
            TrasportOut._start_write_offset = start_offset;
            out->push(TrasportOut);
        } else {
            std::cout << "can't find target" << std::endl;
        }
    }

    std::vector<std::string> get_open_connections() {
        std::vector<std::string> connections;
        while (connected_hostnames_map.empty()) { // LOOP FINCHE NON C'E CONNESIONE
            std::chrono::milliseconds(3);
        }

        for (auto x : connected_hostnames_map) {
            connections.push_back(x.first);
        }
        return connections;
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_H
