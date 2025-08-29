#ifndef CAPIOCOMMUNICATIONSERVICE_HPP
#define CAPIOCOMMUNICATIONSERVICE_HPP

#include <string>

class CapioCommunicationService {

  public:
    ~CapioCommunicationService();

    CapioCommunicationService(std::string &backend_name, const int port,
                              const std::string &control_backend_name);
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_HPP
