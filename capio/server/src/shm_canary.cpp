#include "common/env.hpp"
#include "common/logger.hpp"
#include "remote/discovery.hpp"
#include "utils/common.hpp"

CapioShmCanary::CapioShmCanary(std::string capio_workflow_name)
    : _canary_name(capio_workflow_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(capio_workflow_name: %s)", _canary_name.data());
    if (_canary_name.empty()) {
        _canary_name = get_capio_workflow_name();
    }
    _shm_id = shm_open(_canary_name.data(), O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (_shm_id == -1) {

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Error: canary variable " + _canary_name + " already exists!");
        LOG(CAPIO_SHM_CANARY_ERROR, _canary_name.data());
        ERR_EXIT("ERR: shm canary flag already exists");
    }
}

CapioShmCanary::~CapioShmCanary() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Removing shared memory canary flag");
    close(_shm_id);
    SHM_DESTROY_CHECK(_canary_name.c_str());
}
