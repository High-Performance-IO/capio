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
#include <queue>
#include <random>
#include <string>
#include <unistd.h>

class TransportUnit {
  protected:
    std::string _filepath;
    char *_bytes{};
    capio_off64_t _buffer_size{};
    capio_off64_t _start_write_offset{};

  public:
    TransportUnit() = default;

    ~TransportUnit() { delete[] _bytes; }

    friend class MTCL_backend;
};

class MTCL_backend : public BackendInterface {
    typedef std::tuple<std::queue<TransportUnit *> *, std::queue<TransportUnit *> *, std::mutex *>
        TransportUnitInterface;
    std::unordered_map<std::string, TransportUnitInterface> connected_hostnames_map;
    std::string selfToken, connectedHostname, ownPort, usedProtocol;
    char ownHostname[HOST_NAME_MAX] = {0};
    int thread_sleep_times          = 0;
    bool *continue_execution        = new bool;
    std::mutex *_guard;
    std::thread *th;
    std::vector<std::thread *> connection_threads;
    bool *terminate;

    static TransportUnit *receive_unit(MTCL::HandleUser *HandlerPointer) {
        START_LOG(gettid(), "call()");
        size_t filepath_len;
        const auto unit = new TransportUnit();
        HandlerPointer->receive(&filepath_len, sizeof(size_t));
        unit->_filepath.reserve(filepath_len + 1);
        HandlerPointer->receive(unit->_filepath.data(), filepath_len);
        HandlerPointer->receive(&unit->_buffer_size, sizeof(capio_off64_t));
        unit->_bytes = new char[unit->_buffer_size];
        HandlerPointer->receive(unit->_bytes, unit->_buffer_size);
        HandlerPointer->receive(&unit->_start_write_offset, sizeof(capio_off64_t));
        LOG("[recv] Receiving %ld bytes of file %s", unit->_buffer_size, unit->_filepath.c_str());
        LOG("[recv] Offset of received chunk is %ld", unit->_start_write_offset);
        return unit;
    }

    static void send_unit(MTCL::HandleUser *HandlerPointer, const TransportUnit *unit) {
        START_LOG(gettid(), "call()");
        LOG("[send] buffer=%s", unit->_bytes);
        /**
         * step0: send file path
         * step1: send receive buffer size
         * step1: send offset of write
         * step2: send data
         */
        const size_t file_path_length = unit->_filepath.length();
        HandlerPointer->send(&file_path_length, sizeof(size_t));
        HandlerPointer->send(unit->_filepath.c_str(), file_path_length);
        HandlerPointer->send(&unit->_buffer_size, sizeof(capio_off64_t));
        HandlerPointer->send(unit->_bytes, unit->_buffer_size);
        HandlerPointer->send(&unit->_start_write_offset, sizeof(capio_off64_t));
        LOG("[send] Sent %ld bytes of file %s with offset of %ld", unit->_buffer_size,
            unit->_filepath.c_str(), unit->_start_write_offset);
        delete unit;
    }

    /**
     * This thread will handle connections towards a single target.
     */
    void static server_connection_handler(MTCL::HandleUser HandlerPointer,
                                          const std::string remote_hostname, const int sleep_time,
                                          TransportUnitInterface interface, const bool *terminate) {
        START_LOG(gettid(), "call(remote_hostname=%s)", remote_hostname.c_str());
        // out = data to sent to others
        // in = data from others
        auto [in, out, mutex] = interface;

        while (HandlerPointer.isValid()) {
            // execute up to N operation of send &/or receive, to avoid starvation due to
            // semaphores.
            constexpr int max_net_op = 10;

            // Send phase
            for (int completed_io_operations = 0;
                 completed_io_operations < max_net_op && !out->empty(); ++completed_io_operations) {
                LOG("[send] Starting send section");
                const auto unit = out->front();

                LOG("[send] Sending %ld bytes of file %s to %s", unit->_buffer_size,
                    unit->_filepath.c_str(), remote_hostname.c_str());
                send_unit(&HandlerPointer, unit);
                LOG("[send] Message sent");

                const std::lock_guard lg(*mutex);
                LOG("[send] Locked guard");
                out->pop();
            }

            // Receive phase
            size_t receive_size = 0, completed_io_operations = 0;
            HandlerPointer.probe(receive_size, false);
            while (completed_io_operations < max_net_op && receive_size > 0) {
                LOG("[recv] Receiving data");
                auto unit = receive_unit(&HandlerPointer);
                LOG("[recv] Lock guard");
                const std::lock_guard lg(*mutex);
                in->push(unit);
                LOG("[recv] Pushed %ld bytes to be stored on file %s", unit->_buffer_size,
                    unit->_filepath.c_str());

                ++completed_io_operations;
                receive_size = 0;
                HandlerPointer.probe(receive_size, false);
            }

            // terminate phase
            if (*terminate) {
                const std::lock_guard lg(*mutex);
                LOG("[TERM PHASE] Locked access send and receive queues");
                while (!out->empty()) {
                    const auto unit = out->front();
                    send_unit(&HandlerPointer, unit);
                    out->pop();
                }
                LOG("[TERM PHASE] Emptied queues. Closing connection");
                HandlerPointer.close();
                LOG("[TERM PHASE] Terminating thread server_connection_handler");
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }

    void static incoming_connection_listener(
        const bool *continue_execution, int sleep_time,
        std::unordered_map<std::string, TransportUnitInterface> *open_connections,
        std::mutex *guard, std::vector<std::thread *> *_connection_threads, bool *terminate) {

        char ownHostname[HOST_NAME_MAX] = {0};
        gethostname(ownHostname, HOST_NAME_MAX);

        START_LOG(gettid(), "call(sleep_time=%d, hostname=%s)", sleep_time, ownHostname);

        while (*continue_execution) {
            auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));

            if (!UserManager.isValid()) {
                continue;
            }
            LOG("Handle user is valid");
            char connected_hostname[HOST_NAME_MAX] = {0};
            UserManager.receive(connected_hostname, HOST_NAME_MAX);

            std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << ownHostname << " ] "
                      << "Connected from " << connected_hostname << std::endl;

            LOG("Received connection hostname: %s", connected_hostname);

            const std::lock_guard lock(*guard);

            open_connections->insert(
                {connected_hostname,
                 std::make_tuple(new std::queue<TransportUnit *>(),
                                 new std::queue<TransportUnit *>(), new std::mutex())});

            _connection_threads->push_back(new std::thread(
                server_connection_handler, std::move(UserManager), connected_hostname, sleep_time,
                open_connections->at(connected_hostname), terminate));
        }
    }

  public:
    void connect_to(std::string hostname_port) override {
        START_LOG(gettid(), "call( hostname_port=%s)", hostname_port.c_str());
        std::string remoteHost        = hostname_port.substr(0, hostname_port.find_last_of(':'));
        const std::string remoteToken = usedProtocol + ":" + hostname_port;

        if (remoteToken == selfToken ||                                     // skip on 0.0.0.0
            remoteToken == usedProtocol + ":" + ownHostname + ":" + ownPort // skip on my real IP
        ) {
            LOG("Skipping to connect to self");
            return;
        }

        if (connected_hostnames_map.find(remoteToken) != connected_hostnames_map.end()) {
            LOG("Remote host %s is already connected", remoteHost.c_str());
            return;
        }

        LOG("Trying to connect on remote: %s", remoteToken.c_str());
        if (auto UserManager = MTCL::Manager::connect(remoteToken); UserManager.isValid()) {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << ownHostname << " ] "
                      << "Connected to " << remoteToken << std::endl;
            LOG("Connected to: %s", remoteToken.c_str());
            UserManager.send(ownHostname, HOST_NAME_MAX);
            const std::lock_guard lg(*_guard);

            auto connection_tuple =
                std::make_tuple(new std::queue<TransportUnit *>(),
                                new std::queue<TransportUnit *>(), new std::mutex());
            connected_hostnames_map.insert({remoteHost, connection_tuple});

            connection_threads.push_back(new std::thread(
                server_connection_handler, std::move(UserManager), remoteHost.c_str(),
                thread_sleep_times, connection_tuple, terminate));
        } else {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << ownHostname << " ] "
                      << "Warning: found token " << remoteHost << ".alive_token"
                      << ", but connection is not valid" << std::endl;
        }
    }

    explicit MTCL_backend(const std::string &proto, const std::string &port, int sleep_time)
        : selfToken(proto + ":0.0.0.0:" + port), ownPort(port), usedProtocol(proto),
          thread_sleep_times(sleep_time) {
        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");

        terminate  = new bool;
        *terminate = false;

        _guard = new std::mutex();

        gethostname(ownHostname, HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection %s", ownHostname,
            selfToken.c_str());

        std::string hostname_id("server-");
        hostname_id += ownHostname;
        MTCL::Manager::init(hostname_id);

        *continue_execution = true;

        MTCL::Manager::listen(selfToken);

        th = new std::thread(incoming_connection_listener, std::ref(continue_execution), sleep_time,
                             &connected_hostnames_map, _guard, &connection_threads, terminate);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << ownHostname << " ] "
                  << "MTCL data plane initialization completed." << std::endl;
    }

    ~MTCL_backend() override {
        START_LOG(gettid(), "call()");
        *terminate          = true;
        *continue_execution = false;

        for (const auto thread : connection_threads) {
            thread->join();
        }
        LOG("Terminated connection threads");

        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;
        delete terminate;

        LOG("Handler closed.");

        MTCL::Manager::finalize();
        LOG("Finalizing MTCL backend");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                  << "MTCL backend correctly terminated" << std::endl;
    }

    std::string receive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) override {
        START_LOG(gettid(), "call()");

        std::queue<TransportUnit *> *inQueue = nullptr;
        TransportUnitInterface interface;
        bool found = false;
        while (!found) {
            for (auto [hostname, data] : connected_hostnames_map) {
                inQueue   = std::get<0>(data);
                interface = data;
                found     = !inQueue->empty();
            }
            if (!found) {
                LOG("No incoming messages. Putting thread to sleep");
                std::this_thread::sleep_for(std::chrono::milliseconds(thread_sleep_times));
            }
        }
        LOG("Found incoming message");
        const std::lock_guard lg(*std::get<2>(interface));
        auto inputUnit = inQueue->front();
        *buf_size      = inputUnit->_buffer_size;
        *start_offset  = inputUnit->_start_write_offset;
        memcpy(buf, inputUnit->_bytes, *buf_size);
        LOG("Received buffer: %s", inputUnit->_bytes);
        inQueue->pop();

        std::string filename(inputUnit->_filepath);

        delete inputUnit;
        return filename;
    }

    void send(const std::string &target, char *buf, uint64_t buf_size, const std::string &filepath,
              const capio_off64_t start_offset) override {
        START_LOG(gettid(), "call(target=%s, buf_size=%ld, file_path=%s, start_offset=%ld, buf=%s)",
                  target.c_str(), buf_size, filepath.c_str(), start_offset, buf);

        if (const auto element = connected_hostnames_map.find(target);
            element != connected_hostnames_map.end()) {
            LOG("Found alive connection for given target");

            const auto interface = element->second;
            auto *out            = std::get<1>(interface);

            const auto outputUnit           = new TransportUnit();
            outputUnit->_buffer_size        = buf_size;
            outputUnit->_filepath           = filepath;
            outputUnit->_start_write_offset = start_offset;
            outputUnit->_bytes              = new char[buf_size];
            memcpy(outputUnit->_bytes, buf, buf_size);
            LOG("Copied buffer: %s", outputUnit->_bytes);

            const std::lock_guard lg(*std::get<2>(interface));
            LOG("Pushing Transport unit to out queue");
            out->push(outputUnit);
        } else {
            std::cout << "can't find target" << std::endl;
        }
    }

    std::vector<std::string> get_open_connections() override {
        std::vector<std::string> connections;

        for (const auto &[hostname, _] : connected_hostnames_map) {
            connections.push_back(hostname);
        }
        return connections;
    }
};

#endif // CAPIOCOMMUNICATIONSERVICE_H
