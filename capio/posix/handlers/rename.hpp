#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include <set>

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

int renameat2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    const std::filesystem::path old_dir_fd_path = get_dir_path(static_cast<int>(arg0));
    const std::filesystem::path old_path(reinterpret_cast<const char *>(arg1));
    const std::filesystem::path new_dir_fd_path = get_dir_path(static_cast<int>(arg2));
    const std::filesystem::path new_path(reinterpret_cast<const char *>(arg3));
    const long tid = syscall_no_intercept(SYS_gettid);

    // TODO: implement handling of FLAGS

    START_LOG(
        tid, "call(old_dir_fd_path=%s, old_path=%s, new_dir_fd_path=%s, newpath=%s, flags=%d)",
        old_dir_fd_path.c_str(), old_path.c_str(), new_dir_fd_path.c_str(), new_path.c_str(), arg4);

    // Resolve paths relative to path of dir_fd ONLY if input path is not absolute
    const auto old_path_abs =
        old_path.is_absolute() ? old_path : capio_absolute(old_dir_fd_path / old_path);

    const auto new_path_abs =
        new_path.is_absolute() ? new_path : capio_absolute(new_dir_fd_path / new_path);

    FS_MODE({
        const bool old_is_capio = is_capio_path(old_path_abs);
        const bool new_is_capio = is_capio_path(new_path_abs);
        if (!old_is_capio && !new_is_capio) {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
        // The kernel renames, and checks what the memory variant checks below. Then the
        // descriptors follow the file, and the server moves what it knows of the file (producers,
        // closes) to the new name. An exchange moves no file to a new name: it is not announced.
        *result = syscall_no_intercept(SYS_renameat2, arg0, arg1, arg2, arg3, arg4);
        if (*result < 0 || old_path_abs == new_path_abs || (arg4 & RENAME_EXCHANGE) != 0) {
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (old_is_capio) {
            // The file, or what is inside the directory
            const std::string &old_name = old_path_abs.native();
            std::set<std::string> moved;
            for (const int fd : fd_table->fds()) {
                const std::string path = fd_table->path(fd);
                if (path.compare(0, old_name.size(), old_name) == 0 &&
                    (path.size() == old_name.size() || path[old_name.size()] == '/')) {
                    moved.insert(path);
                }
            }
            for (const auto &path : moved) {
                if (new_is_capio) {
                    fd_table->renamePath(path,
                                         new_path_abs.native() + path.substr(old_name.size()));
                } else {
                    fd_table->removePath(path);
                }
            }
        }
        rename_request_fs(old_path_abs, old_is_capio, new_path_abs, new_is_capio, tid);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })

    if (is_prefix(old_path_abs, new_path_abs)) { // TODO: The check is more complex
        errno   = EINVAL;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    if (is_capio_path(old_path_abs)) {
        fd_table->renamePath(old_path_abs, new_path_abs);
        auto res = rename_request(tid, old_path_abs, new_path_abs);
        *result  = (res < 0 ? -errno : res);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        if (is_capio_path(new_path_abs)) {
            std::filesystem::copy(old_path_abs, new_path_abs);
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        } else {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
    }
}

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // Force flags to zero to ensure that spurious data is being passed to handler
    return renameat2_handler(AT_FDCWD, arg0, AT_FDCWD, arg1, 0, 0, result);
}

int renameat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    // Force flags to zero to ensure that spurious data is being passed to handler
    return renameat2_handler(arg0, arg1, arg2, arg3, 0, arg5, result);
}

#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP
