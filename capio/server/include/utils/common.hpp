#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>
#include <thread>

#include "capiocl_adapter.hpp"
#include "client-manager/client_manager.hpp"
#include "common/dirent.hpp"
#include "storage/manager.hpp"
#include "utils/capio_file.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

void wait_for_dirent_data(off64_t wait_size, int wait_tid, int wait_fd, int wait_count,
                          CapioFile &wait_c_file);

inline void send_dirent_to_client(int tid, int fd, CapioFile &c_file, off64_t offset,
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
        storage_manager->setFileOffset(tid, fd, offset + actual_size);
        return;
    }

    const auto &path_to_check = storage_manager->getPath(tid, fd);
    if (!c_file.is_complete() && CapioCLEngine::get().isFirable(path_to_check)) {
        LOG("File %s has mode no_update and not enough data is available", path_to_check.c_str());
        std::thread t(wait_for_dirent_data, (last_entry + 1) * sizeof(linux_dirent64), tid, fd,
                      count, std::ref(c_file));
        t.detach();
        return;
    }

    client_manager->replyToClient(tid, offset);
}

inline void wait_for_dirent_data(const off64_t wait_size, const int wait_tid, const int wait_fd,
                                 const int wait_count, CapioFile &wait_c_file) {
    const auto current_size = storage_manager->getFileOffset(wait_tid, wait_fd);
    START_LOG(gettid(), "call(wait_size=%d, current_size = %ld, wait_fd=%d, wait_count=%d)",
              wait_size, current_size, wait_fd, wait_count);
    wait_c_file.wait_for_data(wait_size);
    LOG("New capio file size = %ld", wait_c_file.get_stored_size());
    send_dirent_to_client(wait_tid, wait_fd, wait_c_file, current_size, wait_count);
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
