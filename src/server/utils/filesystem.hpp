#ifndef CAPIO_SERVER_UTILS_FILESYSTEM_HPP
#define CAPIO_SERVER_UTILS_FILESYSTEM_HPP

#include <filesystem>
#include <list>

#include <dirent.h>

#include "capio/data_structure.hpp"

#include "capio_file.hpp"
#include "common.hpp"
#include "location.hpp"
#include "metadata.hpp"
#include "types.hpp"

CSClientsRemotePendingReads_t clients_remote_pending_reads;
CSClientsRemotePendingStats_t clients_remote_pending_stat;

void wake_pending_remote_stats(const std::string &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    auto it_client = clients_remote_pending_stat.find(path);
    if (it_client != clients_remote_pending_stat.end()) {
        LOG("File %s has pending threads waiting for its completion", path.c_str());
        for (sem_t *sem : it_client->second) {
            if (sem_post(sem) == -1) {
                ERR_EXIT("error sem_post sem in wake_pending_remote_stats");
            }
            LOG("Woke thread waiting on file %s", path.c_str());
        }
        clients_remote_pending_stat.erase(path);
    } else {
        LOG("File %s has no pending remote stats. continuing", path.c_str());
    }
}

void handle_pending_remote_reads(const std::string &path, off64_t data_size, bool complete) {
    START_LOG(gettid(), "call(%s, %ld, %d)", path.c_str(), data_size, static_cast<int>(complete));

    auto it_client = clients_remote_pending_reads.find(path);
    if (it_client != clients_remote_pending_reads.end()) {
        std::list<std::tuple<size_t, size_t, sem_t *>>::iterator it_list, prev_it_list;
        it_list = it_client->second.begin();
        while (it_list != it_client->second.end()) {
            auto &[offset, nbytes, sem] = *it_list;
            if (complete || (offset + nbytes < data_size)) {
                if (sem_post(sem) == -1) {
                    ERR_EXIT("sem_post sem in "
                             "handle_pending_remote_reads");
                }
                if (it_list == it_client->second.begin()) {
                    it_client->second.erase(it_list);
                    it_list = it_client->second.begin();
                } else {
                    it_client->second.erase(it_list);
                    it_list = std::next(prev_it_list);
                }
            } else {
                prev_it_list = it_list;
                ++it_list;
            }
        }
    }
}

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
    long int ld_size = CAPIO_THEORETICAL_SIZE_DIRENT64;
    ld.d_reclen      = ld_size;

    CapioFile &c_file    = init_capio_file(dir, true);
    void *file_shm       = c_file.get_buffer();
    off64_t file_size    = c_file.get_stored_size();
    off64_t data_size    = file_size + ld_size; // TODO: check theoreitcal size and sizeof(ld) usage
    size_t file_shm_size = c_file.get_buf_size();
    ld.d_off             = data_size;

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
        wake_pending_remote_stats(dir);
    }

    std::string_view mode = c_file.get_mode();
    if (mode == CAPIO_FILE_MODE_NO_UPDATE) {
        handle_pending_remote_reads(dir, data_size, c_file.is_complete());
    }
}

void update_dir(int tid, const std::filesystem::path &file_path, int rank) {
    START_LOG(gettid(), "call(file_path=%s, rank=%d)", file_path.c_str(), rank);
    const std::filesystem::path dir = get_parent_dir_path(file_path);
    CapioFile &c_file               = get_capio_file(dir.c_str());
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(rank, dir, tid);
    }
    write_entry_dir(tid, file_path, dir, 0);
}

off64_t create_dir(int tid, const std::filesystem::path &path, int rank) {
    START_LOG(tid, "call(path=%s, rank=%d)", path.c_str(), rank);

    if (!get_file_location_opt(path)) {
        CapioFile &c_file = create_capio_file(path, true, CAPIO_DEFAULT_DIR_INITIAL_SIZE);
        if (c_file.first_write) {
            c_file.first_write = false;
            // TODO: it works only if there is one prod per file
            if (is_capio_dir(path)) {
                add_file_location(path, node_name, -1);
            } else {
                write_file_location(rank, path, tid);
                update_dir(tid, path, rank);
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
