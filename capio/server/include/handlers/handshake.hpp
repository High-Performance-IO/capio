#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include "clone.hpp"
#include "common/requests.hpp"
#include "fs-streaming/fs_streaming.hpp"

/**
 * The variant of a workflow, on this node, is that of its first client. The reply waits in the
 * response buffer of a refused client until it reads the answer to a request.
 */
inline void refuse_other_variant(const pid_t tid, const pid_t pid, const char *app_name,
                                 const char variant) {
    static char workflow_variant = 0;
    if (variant == 0) {
        return;
    }
    if (workflow_variant == 0) {
        workflow_variant = variant;
        return;
    }
    if (variant != workflow_variant) {
        const auto library = [](const char kind) {
            return kind == CAPIO_VARIANT_FS ? "libcapio_posix_fs.so" : "libcapio_posix.so";
        };
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "Refused process %d (app %s): it uses %s, but this workflow uses %s", pid,
                         app_name, library(variant), library(workflow_variant));
        client_manager->replyToClient(tid, CAPIO_REPLY_VARIANT_REFUSED);
    }
}

void handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    client_manager->registerClient(tid);
}

void handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    char variant = 0;
    sscanf(str, "%d %d %s %d %c", &tid, &pid, app_name, &wait, &variant);
    client_manager->registerClient(tid, app_name, static_cast<bool>(wait));
    FsStreaming::get().addThread(tid, pid, app_name);
    refuse_other_variant(tid, pid, app_name, variant);
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
