#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "capio/data_structure.hpp"

#include "capio_file.hpp"
#include "types.hpp"

inline off64_t store_dirent(char *incoming, char *target_buffer, off64_t incoming_size) {
    START_LOG(gettid(), "call(%s, %s, %to_store)", incoming, target_buffer, incoming_size);
    off64_t stored_size = 0, i = 0;
    struct linux_dirent64 to_store {};
    struct linux_dirent64 *dir_entity;

    to_store.d_reclen = CAPIO_THEORETICAL_SIZE_DIRENT64;
    while (i < incoming_size) {
        dir_entity = (struct linux_dirent64 *) (incoming + i);

        to_store.d_ino  = dir_entity->d_ino;
        to_store.d_off  = stored_size + CAPIO_THEORETICAL_SIZE_DIRENT64;
        to_store.d_type = dir_entity->d_type;

        strcpy(to_store.d_name, dir_entity->d_name);
        memcpy((char *) target_buffer + stored_size, &to_store, sizeof(to_store));

        LOG("DIRENT NAME: %s - TARGET NAME: %s", dir_entity->d_name, to_store.d_name);

        i += CAPIO_THEORETICAL_SIZE_DIRENT64;
        stored_size += to_store.d_reclen;
    }

    return stored_size;
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
