#include <capio/logger.hpp>
#include <climits>
#include <filesystem>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/file-manager/file_manager.hpp>
#include <include/storage-service/capio_storage_service.hpp>

void read_handler(const char *const str) {
    long tid;
    int fd;
    capio_off64_t end_of_read;
    char path[PATH_MAX];
    sscanf(str, "%ld %d %s %llu", &tid, &fd, path, &end_of_read);
    START_LOG(gettid(), "call(path=%s, tid=%ld, end_of_read=%llu)", path, tid, end_of_read);

    const std::filesystem::path path_fs(path);

    /**
     * If process is producer OR fire rule is no update and there is enough data, allow the process
     * to continue in its execution
     */
    const uintmax_t file_size = CapioFileManager::get_file_size_if_exists(path);
    if (capio_cl_engine->isProducer(path, tid) ||
        (capio_cl_engine->isFirable(path_fs) && file_size >= end_of_read)) {
        LOG("File can be consumed as it is either the producer, or the fire rule is FNU and there "
            "is enough data");
        client_manager->reply_to_client(tid, file_size);
        return;
    }

    /**
     * return ULLONG_MAX to signal client cache that file is committed and no more requests are
     * required
     */
    if (CapioFileManager::isCommitted(path)) {
        LOG("File is committed, and hence can be consumed");
        client_manager->reply_to_client(tid, ULLONG_MAX);
        return;
    }

    /**
     * Otherwise, file cannot yet be consumed, and hence add thread to wait for data...
     */
    LOG("File cannot yet be consumed. Adding thread to wait list");
    file_manager->addThreadAwaitingData(path, tid, end_of_read);
}

void read_mem_handler(const char *const str) {
    long int tid;
    capio_off64_t read_size, client_cache_line_size, read_begin_offset;
    int use_cache;
    char path[PATH_MAX];
    sscanf(str, "%ld %llu %llu %llu %d %s", &tid, &read_begin_offset, &read_size,
           &client_cache_line_size, &use_cache, path);
    START_LOG(gettid(),
              "call(tid=%d, read_begin_offset=%llu, read_size=%llu, client_cache_line_size=%llu, "
              "use_cache=%s, path=%s)",
              tid, read_begin_offset, read_size, client_cache_line_size,
              use_cache ? "true" : "false", path);

    capio_off64_t size_to_send = std::min({client_cache_line_size, read_size});
    LOG("Will try to send to client up to %ld bytes", size_to_send);
    auto size_sent = storage_service->reply_to_client(tid, path, read_begin_offset, size_to_send);

    LOG("Sending to posix app the offset up to which read.");
    if (file_manager->isCommitted(path)) {
        LOG("File is committed, setting MSB to 1");
        size_sent |= 0x8000000000000000;
    }

    LOG("Telling client to read %ld bytes", size_sent);
    client_manager->reply_to_client(tid, size_sent);
}
