#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "capio/dirent.hpp"

#include "capio/capio_file.hpp"
#include "capio/metadata.hpp"
#include "capio/types.hpp"
#include "requests.hpp"

void send_data_to_client(int tid, int fd, char *buf, off64_t offset, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, buf=0x%08x, offset=%ld, count=%ld)", tid, fd, buf,
              offset, count);

    write_response(tid, offset + count);
    data_buffers[tid].second->write(buf + offset, count);
    set_capio_file_offset(tid, fd, offset + count);
}

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

        send_data_to_client(tid, fd, reinterpret_cast<char *>(dirents.get()) - offset, offset,
                            actual_size);
    } else {
        write_response(tid, offset);
    }

    return actual_size;
}

inline int find_batch_size(const std::string &glob, CSMetadataConfGlobs_t &metadata_conf_globs) {
    START_LOG(gettid(), "call(%s)", glob.c_str());
    bool found = false;
    int nfiles;
    std::size_t i = 0;

    while (!found && i < metadata_conf_globs.size()) {
        found = glob == std::get<0>(metadata_conf_globs[i]);
        ++i;
    }

    if (found) {
        nfiles = std::get<5>(metadata_conf_globs[i - 1]);
    } else {
        nfiles = -1;
    }

    return nfiles;
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP
