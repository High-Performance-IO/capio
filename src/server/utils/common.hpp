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

off64_t convert_dirent64_to_dirent(char *dirent64_buf, char *dirent_buf, off64_t dirent_64_buf_size) {
    START_LOG(gettid(), "call(%s, %s, %ld)", dirent64_buf, dirent_buf, dirent_64_buf_size);
    off64_t dirent_buf_size = 0;
    off64_t i = 0;
    struct linux_dirent ld;
    struct linux_dirent64 *p_ld64;
    ld.d_reclen = THEORETICAL_SIZE_DIRENT;
    while (i < dirent_64_buf_size) {
        p_ld64 = (struct linux_dirent64 *) (dirent64_buf + i);
        ld.d_ino = p_ld64->d_ino;
        ld.d_off = dirent_buf_size + THEORETICAL_SIZE_DIRENT;
        logfile << "dirent_buf_size " << dirent_buf_size << std::endl;
        strcpy(ld.d_name, p_ld64->d_name);
        ld.d_name[DNAME_LENGTH + 1] = p_ld64->d_type;
        ld.d_name[DNAME_LENGTH] = '\0';
        i += THEORETICAL_SIZE_DIRENT64;
        memcpy((char *) dirent_buf + dirent_buf_size, &ld, sizeof(ld));
        dirent_buf_size += ld.d_reclen;
    }

    return dirent_buf_size;
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

void
update_metadata_conf(std::string &path, size_t pos, long int n_files, size_t batch_size, const std::string &committed,
                     const std::string &mode, const std::string &app_name, bool permanent, long int n_close,
                     CSMetadataConfGlobs_t *metadata_conf_globs, CSMetadataConfMap_t *metadata_conf) {

    START_LOG(gettid(), "call(%s, %ld, %ld, %ld, %s, %s, %s, &d, %ld)", path.c_str(), pos, n_files, batch_size,
              committed.c_str(), mode.c_str(), app_name.c_str(), static_cast<int>(permanent), n_close);

    if (pos == std::string::npos && n_files == -1)
        (*metadata_conf)[path] = std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
    else {
        std::string prefix_str = path.substr(0, pos);
        metadata_conf_globs->emplace_back(prefix_str, committed, mode, app_name, n_files, batch_size, permanent, n_close);

    }

}

long int match_globs(const std::string& path, CSMetadataConfGlobs_t *metadata_conf_globs) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    long int res = -1;
    size_t i = 0;
    size_t max_length_prefix = 0;
    while (i < metadata_conf_globs->size()) {
        std::string prefix_str = std::get<0>((*metadata_conf_globs)[i]);
        size_t prefix_length = prefix_str.length();
        if (path.compare(0, prefix_length, prefix_str) == 0 && prefix_length > max_length_prefix) {
            res = i;
            max_length_prefix = prefix_length;
        }
        ++i;
    }

    return res;
}

void clean_files_location(int n_servers) {
    START_LOG(gettid(), "call(%d)", n_servers);
    std::string file_name;
    for (int rank = 0; rank < n_servers; ++rank) {
        std::string rank_str = std::to_string(rank);
        file_name = "files_location_" + rank_str + ".txt";
        remove(file_name.c_str());
    }

}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP
