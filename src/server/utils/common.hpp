#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "capio/data_structure.hpp"

#include "capio_file.hpp"
#include "requests.hpp"
#include "types.hpp"

void send_data_to_client(int tid, char *buf, long int count) {
    START_LOG(gettid(), "call(%d,%.10s, %ld)", tid, buf, count);
    auto *data_buf  = data_buffers[tid].second;
    size_t n_writes = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t r        = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t i        = 0;
    while (i < n_writes) {
        data_buf->write(buf + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
        ++i;
    }
    if (r) {
        data_buf->write(buf + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
    }
}

inline off64_t send_dirent_to_client(int tid, CapioFile &c_file, off64_t offset, off64_t count) {
    START_LOG(gettid(), "call(offset=%ld, count=%ld)", offset, count);

    struct linux_dirent64 *dir_entity;
    struct linux_dirent64 to_store {};
    to_store.d_reclen = CAPIO_THEORETICAL_SIZE_DIRENT64;

    char *incoming      = c_file.get_buffer();
    off64_t n_entries   = c_file.get_stored_size() / CAPIO_THEORETICAL_SIZE_DIRENT64;
    off64_t actual_size = n_entries * CAPIO_THEORETICAL_SIZE_DIRENT64;
    char *p_getdents    = (char *) malloc(actual_size * sizeof(char));

    off64_t stored_size = 0;
    for (int i = 0; i < n_entries; i++) {
        dir_entity = (struct linux_dirent64 *) (incoming + i * CAPIO_THEORETICAL_SIZE_DIRENT64);

        to_store.d_ino  = dir_entity->d_ino;
        to_store.d_off  = stored_size + CAPIO_THEORETICAL_SIZE_DIRENT64;
        to_store.d_type = dir_entity->d_type;

        strcpy(to_store.d_name, dir_entity->d_name);
        memcpy((char *) p_getdents + stored_size, &to_store, sizeof(to_store));

        LOG("DIRENT NAME: %s - TARGET NAME: %s", dir_entity->d_name, to_store.d_name);

        stored_size += CAPIO_THEORETICAL_SIZE_DIRENT64;
    }

    int res = 0;
    while (res + CAPIO_THEORETICAL_SIZE_DIRENT64 <= std::min(actual_size - offset, count)) {
        res += CAPIO_THEORETICAL_SIZE_DIRENT64;
    }
    actual_size = res;

    write_response(tid, offset + actual_size);
    send_data_to_client(tid, p_getdents + offset, actual_size);
    free(p_getdents);

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
