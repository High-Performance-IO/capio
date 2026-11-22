#ifndef CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP
#define CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP

#include "fs-streaming/fs_streaming.hpp"

/*
 * Requests of the FS variant end with the path, which may contain spaces, so the handlers read the
 * arguments before it with sscanf and a final %n, and the path from there to the end.
 */

/// @brief The path of a request of the FS variant, whose arguments end at @p consumed
inline std::string fs_request_path(const pid_t tid, const char *str, const int consumed) {
    // %n follows the last number: one space separates it from the path
    return FsStreaming::get().takePath(tid,
                                       str[consumed] == ' ' ? str + consumed + 1 : str + consumed);
}

void path_part_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1 && str[consumed] == ' ') {
        FsStreaming::get().addPathPart(tid, str + consumed + 1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP
