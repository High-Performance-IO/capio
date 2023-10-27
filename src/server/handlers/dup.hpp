#ifndef CAPIO_SERVER_HANDLERS_DUP_HPP
#define CAPIO_SERVER_HANDLERS_DUP_HPP

#include "utils/metadata.hpp"

void dup_handler(const char *const str, int rank) {
    int tid;
    int old_fd, new_fd;
    sscanf(str, "%d %d %d", &tid, &old_fd, &new_fd);
    START_LOG(gettid(), "call(tid=%d, old_fd=%d, new_fd=%d)", tid, old_fd, new_fd);

    dup_capio_file(tid, old_fd, new_fd);
}

#endif // CAPIO_SERVER_HANDLERS_DUP_HPP
