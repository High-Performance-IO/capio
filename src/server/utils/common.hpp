#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "capio/data_structure.hpp"
#include "capio_file.hpp"
#include "types.hpp"

char *expand_memory_for_file(const std::string &path, off64_t data_size, Capio_file &c_file) {
    char *new_p = c_file.expand_buffer(data_size);
    return new_p;
}

inline off64_t store_dirent(char *incoming, char *target_buffer, off64_t incoming_size) {
    START_LOG(gettid(), "call(%s, %s, %to_store)", incoming, target_buffer, incoming_size);
    off64_t stored_size = 0, i = 0;
    struct linux_dirent64 to_store {};
    struct linux_dirent64 *dir_entity;

    to_store.d_reclen = THEORETICAL_SIZE_DIRENT64;
    while (i < incoming_size) {
        dir_entity = (struct linux_dirent64 *) (incoming + i);

        to_store.d_ino  = dir_entity->d_ino;
        to_store.d_off  = stored_size + THEORETICAL_SIZE_DIRENT64;
        to_store.d_type = dir_entity->d_type;

        strcpy(to_store.d_name, dir_entity->d_name);
        memcpy((char *) target_buffer + stored_size, &to_store, sizeof(to_store));

        LOG("DIRENT NAME: %s - TARGET NAME: %s", dir_entity->d_name, to_store.d_name);

        i += THEORETICAL_SIZE_DIRENT64;
        stored_size += to_store.d_reclen;
    }

    return stored_size;
}

bool is_int(const std::string &s) {
    START_LOG(gettid(), "call(%s)", s.c_str());
    bool res = false;
    if (!s.empty()) {
        char *p;
        strtol(s.c_str(), &p, 10);
        res = *p == 0;
    }
    return res;
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP
