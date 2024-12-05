#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H

#include <mtcl.hpp>

#include "BackendInterface.hpp"

class CapioCommunicationService {
    std::thread *th;

    bool *continue_execution = new bool;

    static void _main(const bool *continue_execution) {

        START_LOG(gettid(), "INFO: instance of FileSystemMonitor");

        timespec sleep{};
        sleep.tv_nsec = 300; // sleep 0.3 seconds
        while (*continue_execution) {
            // TODO: add execution of backend
            nanosleep(&sleep, nullptr);
        }
    }

  public:
    explicit CapioCommunicationService() {
        START_LOG(gettid(), "call()");
        *continue_execution = true;
        th                  = new std::thread(_main, std::ref(continue_execution));
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
    }

    /**
     *
     * @param target
     * @param buffer
     * @param buffer_size
     * @param offset
     */
    void sendData(const std::string &target, char *buffer, capio_off64_t buffer_size,
                  capio_off64_t offset) {}
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_H
