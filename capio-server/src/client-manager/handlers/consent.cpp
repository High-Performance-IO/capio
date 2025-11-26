#include <capio/logger.hpp>
#include <climits>
#include <filesystem>
#include <capiocl.hpp>
#include <engine.h>
#include <include/file-manager/file_manager.hpp>
#include <include/client-manager/client_manager.hpp>
extern capiocl::engine::Engine *capio_cl_engine;

void consent_to_proceed_handler(const char *const str) {
    pid_t tid;
    char path[1024], source_func[1024];
    sscanf(str, "%d %s %s", &tid, path, source_func);
    START_LOG(gettid(), "call(tid=%d, path=%s, source=%s)", tid, path, source_func);

    const auto app_name = client_manager->get_app_name(tid);

    // Skip operations on CAPIO_DIR
    if (!capio_cl_engine->contains(path)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (capio_cl_engine->isExcluded(path)) {
        LOG("File should NOT be handled by CAPIO as it is marked as EXCLUDED");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (capio_cl_engine->isProducer(path, app_name)) {
        LOG("Application is producer. continuing");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (!std::filesystem::exists(path)) {
        LOG("Requested file %s does not exists yet. awaiting for creation", path);
        file_manager->addThreadAwaitingCreation(path, tid);
        return;
    }

    if (capio_cl_engine->isFirable(path)) {
        LOG("Mode for file %s is no_update. allowing process to continue", path);
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (CapioFileManager::isCommitted(path)) {
        LOG("It is possible to unlock waiting thread");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    LOG("File %s is not yet committed. Adding to threads waiting for committed with  ULLONG_MAX",
        path);
    file_manager->addThreadAwaitingData(path, tid, ULLONG_MAX);
}
