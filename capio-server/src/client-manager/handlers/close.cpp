#include <filesystem>
#include <climits>
#include <capio/logger.hpp>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/file-manager/file_manager.hpp>


void close_handler(const char *const str) {
    pid_t tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    const std::filesystem::path filename(path);

    LOG("File needs handling");

    // Call the set_committed method only if the commit rule is on_close and calling thread is a
    // producer
    if (capio_cl_engine->getCommitRule(filename) == CAPIO_FILE_COMMITTED_ON_CLOSE &&
        capio_cl_engine->isProducer(filename, tid)) {
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
