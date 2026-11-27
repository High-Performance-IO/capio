#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include "clone.hpp"
#include "common/requests.hpp"
#include "utils/common.hpp"

/**
 * @brief Whether a client of @p variant is refused: the variant of a workflow, on this node, is
 * that of its first client, and the two variants cannot be mixed yet.
 * TODO: drop the refusal once the two variants work together in one workflow (#337)
 */
inline bool refuses_variant(const pid_t pid, const char *app_name, const char variant) {
    static char workflow_variant = 0;
    if (variant == 0) {
        return false;
    }
    if (workflow_variant == 0) {
        workflow_variant = variant;
        return false;
    }
    if (variant == workflow_variant) {
        return false;
    }
    const auto library = [](const char kind) {
        return kind == CAPIO_VARIANT_FS ? "libcapio_posix_fs.so" : "libcapio_posix.so";
    };
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                     "Refused process %d (app %s): it uses %s, but this workflow uses %s", pid,
                     app_name, library(variant), library(workflow_variant));
    return true;
}

/**
 * @brief "tid pid app_name wait variant". Every handshake gets one answer: 1, or
 * CAPIO_REPLY_VARIANT_REFUSED for a client of the other variant. A new thread (wait) is answered
 * only once its parent's clone request is handled (ClientManager::unlockClonedChild).
 */
void handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    char variant = 0;
    sscanf(str, "%d %d %s %d %c", &tid, &pid, app_name, &wait, &variant);
    const bool refused = refuses_variant(pid, app_name, variant);
    client_manager->registerClient(tid, app_name, wait != 0 && !refused, pid);
    if (refused) {
        client_manager->replyToClient(tid, CAPIO_REPLY_VARIANT_REFUSED);
    } else if (wait == 0) {
        client_manager->replyToClient(tid, 1);
    }
}

/**
 * @brief "tid path": the CAPIO_DIR of a client, sent once by each process after its handshake.
 * No reply.
 */
void root_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    storage_manager->addRoot(tid, request_text(tid, str, consumed));
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
