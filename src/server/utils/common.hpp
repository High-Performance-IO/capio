#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "capio_file.hpp"
#include "data_structure.hpp"
#include "types.hpp"

char *expand_memory_for_file(const std::string &path, off64_t data_size, Capio_file &c_file) {
    char *new_p = c_file.expand_buffer(data_size);
    return new_p;
}

off64_t convert_dirent64_to_dirent(char *dirent64_buf, char *dirent_buf,
                                   off64_t dirent_64_buf_size, bool is_dir) {
    START_LOG(gettid(), "call(%s, %s, %ld)", dirent64_buf, dirent_buf, dirent_64_buf_size);
    off64_t dirent_buf_size = 0;
    off64_t i               = 0;
    struct linux_dirent ld;
    struct linux_dirent64 *p_ld64;
    ld.d_reclen = THEORETICAL_SIZE_DIRENT;
    while (i < dirent_64_buf_size) {
        p_ld64   = (struct linux_dirent64 *) (dirent64_buf + i);
        ld.d_ino = p_ld64->d_ino;
        ld.d_off = dirent_buf_size + THEORETICAL_SIZE_DIRENT;

        strcpy(ld.d_name, p_ld64->d_name);
        ld.d_name[DNAME_LENGTH]     = '\0';
        ld.d_type = p_ld64->d_type;

        i += THEORETICAL_SIZE_DIRENT64;
        memcpy((char *) dirent_buf + dirent_buf_size, &ld, sizeof(ld));
        dirent_buf_size += ld.d_reclen;
    }

    return dirent_buf_size;
}

off64_t compute_dirent_offset(){
    return 0;
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
