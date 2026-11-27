#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include "clone.hpp"
#include "common/requests.hpp"
#include "utils/common.hpp"
#include "utils/fs_root.hpp"
#include "utils/runtime_configuration.hpp"

#include "capiocl/engine.h"

extern capiocl::engine::Engine *capio_cl_engine;

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
 * @brief Whether an FS client whose CAPIO_DIR is @p capio_dir is refused. The server streams
 * through one directory, the real one behind the virtual "/": -r, or else the CAPIO_DIR of its
 * first FS client, which then starts the file system monitor there. Any other is refused.
 */
inline bool refuses_root(const pid_t pid, const char *app_name,
                         const std::filesystem::path &capio_dir) {
    if (fs_root().known()) {
        if (fs_root().get() == capio_dir) {
            return false;
        }
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "Refused process %d (app %s): its CAPIO_DIR is %s, but this server "
                         "streams through %s",
                         pid, app_name, capio_dir.c_str(), fs_root().get().c_str());
        return true;
    }
    if (std::error_code error; !std::filesystem::is_directory(capio_dir, error)) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "Refused process %d (app %s): its CAPIO_DIR %s is not a directory here",
                         pid, app_name, capio_dir.c_str());
        return true;
    }
    fs_root().set(capio_dir);
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                     "No -r given: streaming through %s, the CAPIO_DIR of process %d (app %s)",
                     capio_dir.c_str(), pid, app_name);
    capio_cl_engine->startFileSystemMonitor(capio_dir,
                                            RuntimeConfiguration::get().getMetadataDir(capio_dir));
    return false;
}

/**
 * @brief "tid pid app_name wait variant capio_dir", where capio_dir is the real CAPIO_DIR of an FS
 * client, empty for a memory one. Every handshake gets one answer: 1, CAPIO_REPLY_VARIANT_REFUSED
 * for a client of the other variant, or CAPIO_REPLY_ROOT_REFUSED for an FS client whose CAPIO_DIR
 * is not the directory the server streams through. A new thread (wait) is answered only once its
 * parent's clone request is handled (ClientManager::unlockClonedChild).
 */
inline void handshake_named_handler(const char *const str) {
    int tid, pid, wait, consumed = 0;
    char app_name[1024];
    char variant = 0;
    if (sscanf(str, "%d %d %1023s %d %c%n", &tid, &pid, app_name, &wait, &variant, &consumed) !=
        5) {
        return;
    }
    const std::filesystem::path capio_dir = request_text(tid, str, consumed);
    const bool wrong_variant              = refuses_variant(pid, app_name, variant);
    const bool wrong_root =
        !wrong_variant && variant == CAPIO_VARIANT_FS && refuses_root(pid, app_name, capio_dir);
    const bool refused = wrong_variant || wrong_root;
    client_manager->registerClient(tid, app_name, wait != 0 && !refused, pid);
    if (wrong_variant) {
        client_manager->replyToClient(tid, CAPIO_REPLY_VARIANT_REFUSED);
    } else if (wrong_root) {
        client_manager->replyToClient(tid, CAPIO_REPLY_ROOT_REFUSED);
    } else if (wait == 0) {
        client_manager->replyToClient(tid, 1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
