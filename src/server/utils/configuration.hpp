#ifndef CAPIO_CONFIGURATION_HPP
#define CAPIO_CONFIGURATION_HPP

#include <limits.h>
#include <string>

/*
 * Variables required to be globally available
 * to all classes and subclasses.
 */
class CapioGlobalConfiguration {
  public:
    bool termination_phase, StoreOnlyInMemory;
    std::string workflow_name;
    pid_t CAPIO_SERVER_MAIN_PID = -1;
    char node_name[HOST_NAME_MAX]{0};

    CapioGlobalConfiguration() {
        termination_phase = false;
        StoreOnlyInMemory = false;
        gethostname(node_name, HOST_NAME_MAX);
        CAPIO_SERVER_MAIN_PID = gettid();
    }
};

inline auto capio_global_configuration = new CapioGlobalConfiguration();

#endif // CAPIO_CONFIGURATION_HPP
