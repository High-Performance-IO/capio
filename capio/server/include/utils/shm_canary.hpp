#ifndef CAPIO_SHM_CANARY_HPP
#define CAPIO_SHM_CANARY_HPP
#include <string>

class CapioShmCanary {
    int _shm_id;
    std::string _canary_name;

  public:
    explicit CapioShmCanary(const std::string &capio_workflow_name);
    ~CapioShmCanary();
};
#endif // CAPIO_SHM_CANARY_HPP
