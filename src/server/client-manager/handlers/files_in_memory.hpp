#ifndef FILES_IN_MEMORY_HPP
#define FILES_IN_MEMORY_HPP

/**
 * @brief Handle the exit systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void files_to_store_in_memory_handler(const char *const str) {
    // TODO: register files open for each tid ti register a close
    pid_t tid;
    sscanf(str, "%d", &tid);
    START_LOG(gettid(), "call(tid=%d)", tid);

    auto count = storage_service->sendFilesToStoreInMemory(tid);

    LOG("Need to tell client to read %llu files from data queue", count);
    client_manager->reply_to_client(tid, count);
}

#endif // FILES_IN_MEMORY_HPP