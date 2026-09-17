#include "utils/shm_canary.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "common/constants.hpp"
#include "common/env.hpp"
#include "common/shm.hpp"
#include "utils/common.hpp"

CapioShmCanary::CapioShmCanary(const std::string &capio_workflow_name)
    : _canary_name(capio_workflow_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(capio_workflow_name: %s)", _canary_name.data());
    if (_canary_name.empty()) {
        _canary_name = get_capio_workflow_name();
    }
    _shm_id = shm_open(_canary_name.data(), O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (_shm_id == -1) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Error: canary variable %s already exists!",
                         _canary_name.c_str());
        LOG(CAPIO_SHM_CANARY_ERROR, _canary_name.data());
        ERR_EXIT("ERR: shm canary flag already exists");
    }
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_STATUS, "Shared memory canary created: %s",
                     _canary_name.c_str());
}

CapioShmCanary::~CapioShmCanary() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "Removing shared memory canary flag");
    close(_shm_id);
    SHM_DESTROY_CHECK(_canary_name.c_str());
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Shared memory canary destroyed: %s",
                     _canary_name.c_str());
}
