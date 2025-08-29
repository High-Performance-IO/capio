#ifndef CAPIOCOMMUNICATIONSERVICE_HPP
#define CAPIOCOMMUNICATIONSERVICE_HPP

#include "control_plane/capio_control_plane.hpp"
#include "data_plane/BackendInterface.hpp"

#include "control_plane/fs_control_plane.hpp"
#include "control_plane/multicast_control_plane.hpp"
#include "data_plane/MTCL_backend.hpp"

class CapioCommunicationService {

  public:
    ~CapioCommunicationService() {
        delete capio_control_plane;
        delete capio_backend;
    };

    CapioCommunicationService(std::string &backend_name, const int port,
                              const std::string &control_backend_name) {
        START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());

        LOG("My hostname is %s. Starting to listen on connection",
            capio_global_configuration->node_name);

        if (backend_name == "MQTT" || backend_name == "MPI") {
            server_println(
                CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                "Warn: selected backend is not yet officially supported. Setting backend to TCP");
            backend_name = "TCP";
        }

        if (backend_name == "TCP" || backend_name == "UCX") {

            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Selected backend is " + backend_name);
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                           "Selected backend port is " + std::to_string(port));
            capio_backend = new MTCL_backend(backend_name, std::to_string(port),
                                             CAPIO_BACKEND_DEFAULT_SLEEP_TIME);
        } else if (backend_name == "FS") {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Selected backend is File System");
            capio_backend = new NoBackend();
        } else if (backend_name == "none") {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                           "Skipping communication backend startup");
        } else {
            START_LOG(gettid(), "call()");
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           "Provided communication backend " + backend_name + " is invalid");
            ERR_EXIT("No valid backend was provided");
        }

        server_println(CAPIO_SERVER_CLI_LOG_SERVER,
                       "CapioCommunicationService initialization completed.");

        if (control_backend_name == "fs") {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Starting FS control plane");
            capio_control_plane = new FSControlPlane(port);
        } else if (control_backend_name == "multicast") {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Starting multicast control plane");
            capio_control_plane = new MulticastControlPlane(port);
        } else {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                           "Error: unknown control plane backend: " + control_backend_name);
        }
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_HPP
