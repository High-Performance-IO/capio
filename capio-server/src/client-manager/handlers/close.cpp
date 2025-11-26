#include "include/client-manager/client_manager.hpp"

#include <capio/logger.hpp>
#include <capiocl.hpp>
#include <engine.h>
#include <climits>
#include <filesystem>
#include <include/file-manager/file_manager.hpp>
extern capiocl::engine::Engine *capio_cl_engine;

void close_handler(const char *const str) {
    pid_t tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    const std::filesystem::path filename(path);
    const auto app_name = client_manager->get_app_name(tid);

    LOG("File needs handling");

    // Call the set_committed method only if the commit rule is on_close and calling thread is a
    // producer
    if (capio_cl_engine->getCommitRule(filename) == CAPIO_FILE_COMMITTED_ON_CLOSE &&
        capio_cl_engine->isProducer(filename, app_name)) {
        CapioFileManager::setCommitted(path);
        /**
         * The increase close count is called only on explicit close() sc, as defined by the
         * CAPIO-CL specification. If it were to be called every time the file is committed, then
         * an extra increase would occur as by default, at termination all files are committed.
         * By calling this only when close sc are occurred, we guarantee the correct count of
         * how many close sc occurs. Also, checks are computed to increase the count only if the
         * commit count is greater than 1 to avoid unnecessary overhead.
         */
        if (capio_cl_engine->getCommitCloseCount(filename) > 1) {
            CapioFileManager::increaseCloseCount(path);
        }
    }
}
