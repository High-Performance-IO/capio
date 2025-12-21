#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "common/constants.hpp"
#include "common/dirent.hpp"

#include "utils/capio_file.hpp"
#include "utils/metadata.hpp"
#include "utils/types.hpp"

#include "client-manager/client_manager.hpp"
extern ClientManager *client_manager;

inline off64_t send_dirent_to_client(int tid, int fd, CapioFile &c_file, off64_t offset,
                                     off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld, count=%ld)", tid, fd, offset, count);

    struct linux_dirent64 *dir_entity;

    char *incoming      = c_file.get_buffer();
    int first_entry     = static_cast<int>(offset / sizeof(linux_dirent64));
    off64_t end_of_read = std::min(offset + count, c_file.get_stored_size());
    int last_entry      = static_cast<int>(end_of_read / sizeof(linux_dirent64));
    off64_t actual_size = (last_entry - first_entry) * static_cast<off64_t>(sizeof(linux_dirent64));

    if (actual_size > 0) {
        auto dirents = std::unique_ptr<linux_dirent64[]>(new linux_dirent64[actual_size]);

        for (int i = first_entry; i < last_entry; i++) {
            dir_entity = (struct linux_dirent64 *) (incoming + i * sizeof(linux_dirent64));
            auto &current_dirent = dirents[i - first_entry];

            current_dirent.d_reclen = sizeof(linux_dirent64);
            current_dirent.d_ino    = dir_entity->d_ino;
            current_dirent.d_off    = (i + 1) * current_dirent.d_reclen;
            current_dirent.d_type   = dir_entity->d_type;
            strcpy(current_dirent.d_name, dir_entity->d_name);

            LOG("DIRENT NAME: %s - TARGET NAME: %s", dir_entity->d_name, current_dirent.d_name);
        }

        client_manager->replyToClient(tid, offset, reinterpret_cast<char *>(dirents.get()) - offset,
                                      actual_size);
        set_capio_file_offset(tid, fd, offset + actual_size);

    } else {
        client_manager->replyToClient(tid, offset);
    }

    return actual_size;
}

inline bool is_int(const std::string &s) {
    START_LOG(gettid(), "call(%s)", s.c_str());
    bool res = false;
    if (!s.empty()) {
        char *p;
        strtol(s.c_str(), &p, 10);
        res = *p == 0;
    }
    return res;
}

inline void server_println(const std::string &message_type = "",
                           const std::string &message_line = "") {
    if (message_type.empty()) {
        std::cout << std::endl;
    } else {
        std::cout << message_type << " " << get_capio_workflow_name() << "] " << message_line
                  << std::endl
                  << std::flush;
    }
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP
