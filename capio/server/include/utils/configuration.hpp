#ifndef CAPIO_CONFIGURATION_HPP
#define CAPIO_CONFIGURATION_HPP

#include "common/constants.hpp"
#include <iostream>
#include <string>
#include <unistd.h>

/*
 * Variables required to be globally available
 * to all classes and subclasses.
 */
class CapioGlobalConfiguration {
  public:
    bool termination_phase;
    bool store_only_in_memory;
    std::string workflow_name;
    pid_t capio_server_main_pid = -1;
    char node_name[HOST_NAME_MAX]{0};

    CapioGlobalConfiguration() {
        gethostname(node_name, HOST_NAME_MAX);
        termination_phase     = false;
        store_only_in_memory  = false;
        capio_server_main_pid = gettid();
        workflow_name         = CAPIO_DEFAULT_WORKFLOW_NAME;
    }
};

inline auto capio_global_configuration = new CapioGlobalConfiguration();

inline void server_println(const std::string &message_type = "",
                           const std::string &message_line = "") {
    if (message_type.empty()) {
        std::cout << std::endl;
    } else {
        std::cout << message_type << " " << capio_global_configuration->node_name << "] "
                  << message_line << std::endl
                  << std::flush;
    }
}

#endif // CAPIO_CONFIGURATION_HPP