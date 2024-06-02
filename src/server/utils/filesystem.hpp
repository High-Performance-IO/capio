#ifndef CAPIO_SERVER_UTILS_FILESYSTEM_HPP
#define CAPIO_SERVER_UTILS_FILESYSTEM_HPP

#include <filesystem>
#include <list>

#include <dirent.h>

#include "capio/dirent.hpp"

#include "capio_file.hpp"
#include "common.hpp"
#include "location.hpp"
#include "metadata.hpp"
#include "types.hpp"

/*
 * type == 0 -> regular entry
 * type == 1 -> "." entry
 * type == 2 -> ".." entry
 */

void write_entry_dir(int tid, const std::filesystem::path &file_path,
                     const std::filesystem::path &dir, int type) {
    START_LOG(gettid(), "call(file_path=%s, dir=%s, type=%d)", file_path.c_str(), dir.c_str(),
              type);

    struct linux_dirent64 ld {};
    ld.d_ino = std::hash<std::string>{}(file_path);
    std::filesystem::path file_name;
    if (type == 0) {
        file_name = file_path.filename();
        LOG("FILENAME: %s", file_name.c_str());
    } else if (type == 1) {
        file_name = ".";
    } else {
        file_name = "..";
    }

    strcpy(ld.d_name, file_name.c_str());
    LOG("FILENAME LD: %s", ld.d_name);
    ld.d_reclen = sizeof(linux_dirent64);

    CapioFile &c_file = get_capio_file(dir);
    c_file.create_buffer_if_needed(true);

    off64_t file_size    = c_file.get_stored_size();
    off64_t data_size    = file_size + ld.d_reclen;
    size_t file_shm_size = c_file.get_buf_size();
    ld.d_off             = data_size;
    void *file_shm       = c_file.get_buffer();

    if (data_size > file_shm_size) {
        file_shm = c_file.expand_buffer(data_size);
    }

    ld.d_type = (c_file.is_dir() ? DT_DIR : DT_REG);

    memcpy((char *) file_shm + file_size, &ld, sizeof(ld));
    off64_t base_offset = file_size;

    LOG("STORED FILENAME LD: %s",
        ((struct linux_dirent64 *) ((char *) file_shm + file_size))->d_name);

    c_file.insert_sector(base_offset, data_size);
    ++c_file.n_files;
    int pid           = pids[tid];
    writers[pid][dir] = true;

    if (c_file.n_files == c_file.n_files_expected) {
        c_file.set_complete();
    }
}

void update_dir(int tid, const std::filesystem::path &file_path) {
    START_LOG(gettid(), "call(file_path=%s)", file_path.c_str());
    const std::filesystem::path dir = get_parent_dir_path(file_path);
    CapioFile &c_file               = get_capio_file(dir.c_str());
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(dir);
    }
    write_entry_dir(tid, file_path, dir, 0);
}

off64_t create_dir(int tid, const std::filesystem::path &path) {
    START_LOG(tid, "call(path=%s)", path.c_str());

    if (!get_file_location_opt(path)) {
        CapioFile &c_file = create_capio_file(path, true, CAPIO_DEFAULT_DIR_INITIAL_SIZE);
        if (c_file.first_write) {
            c_file.first_write = false;
            // TODO: it works only if there is one prod per file
            if (is_capio_dir(path)) {
                add_file_location(path, node_name, -1);
            } else {
                write_file_location(path);
                update_dir(tid, path);
            }
            write_entry_dir(tid, path, path, 1);
            const std::filesystem::path parent_dir = get_parent_dir_path(path);
            write_entry_dir(tid, parent_dir, path, 2);
        }
        return 0;
    } else {
        return 1;
    }
}

#endif // CAPIO_SERVER_UTILS_FILESYSTEM_HPP
