#ifndef CAPIOCOMMUNICATIONSERVICE_HPP
#define CAPIOCOMMUNICATIONSERVICE_HPP

#include "control_plane/capio_control_plane.hpp"
#include "data_plane/BackendInterface.hpp"

#include "control_plane/fs_control_plane.hpp"
#include "control_plane/multicast_control_plane.hpp"
#include "data_plane/MTCL_backend.hpp"

class CapioCommunicationService {

    char ownHostname[HOST_NAME_MAX] = {0};

  public:
    ~CapioCommunicationService() {
        delete capio_control_plane;
        delete capio_backend;
    };

    CapioCommunicationService(std::string &backend_name, const int port,
                              const std::string &control_plane_backend = "multicast") {
        START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());
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

        if (control_plane_backend == "multicast") {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                      << "Starting multicast control plane" << std::endl;
            capio_control_plane = new MulticastControlPlane(port);
        } else {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                      << "Starting file system control plane" << std::endl;
            capio_control_plane = new FSControlPlane(port);
        }

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << ownHostname << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_HPP
