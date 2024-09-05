#ifndef CAPIO_EXIT_HPP
#define CAPIO_EXIT_HPP

inline void exit_handler(const char *const str) {
    // TODO: register files open for each tid ti register a close
    pid_t tid;
    sscanf(str, "%d", &tid);
    START_LOG(gettid(), "call(tid=%d)", tid);
    file_manager->set_committed(tid);
    client_manager->remove_client(tid);
}

#endif // CAPIO_EXIT_HPP
