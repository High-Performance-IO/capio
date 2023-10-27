#ifndef CAPIO_UTIL_FILESYS_HPP
#define CAPIO_UTIL_FILESYS_HPP

#include "utils/location.hpp"

void reply_remote_stats(const std::string &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    auto it_client = clients_remote_pending_stat.find(path);
    if (it_client != clients_remote_pending_stat.end()) {
        for (sem_t *sem : it_client->second) {
            if (sem_post(sem) == -1) {
                ERR_EXIT("error sem_post sem in reply_remote_stats");
            }
        }
        clients_remote_pending_stat.erase(path);
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

void write_entry_dir(int tid, const std::string &file_path, const std::string &dir, int type) {
    START_LOG(tid, "call(file_path=%s, dir=%s, type=%d)", file_path.c_str(), dir.c_str(), type);

    std::hash<std::string> hash;
    struct linux_dirent64 ld;
    ld.d_ino = hash(file_path);
    std::string file_name;
    if (type == 0) {
        std::size_t i = file_path.rfind('/');
        if (i == std::string::npos) {
            logfile << "invalid file_path in get_parent_dir_path" << std::endl;
        }
        file_name = file_path.substr(i + 1);
    } else if (type == 1) {
        file_name = ".";
    } else {
        file_name = "..";
    }

    strcpy(ld.d_name, file_name.c_str());
    long int ld_size = THEORETICAL_SIZE_DIRENT64;
    ld.d_reclen      = ld_size;

    Capio_file &c_file   = init_capio_file(dir.c_str(), true);
    void *file_shm       = c_file.get_buffer();
    off64_t file_size    = c_file.get_stored_size();
    off64_t data_size    = file_size + ld_size; // TODO: check theoreitcal size and sizeof(ld) usage
    size_t file_shm_size = c_file.get_buf_size();
    ld.d_off             = data_size;

    if (data_size > file_shm_size) {
        file_shm = expand_memory_for_file(dir, data_size, c_file);
    }
    if (c_file.is_dir()) {
        ld.d_type = DT_DIR;
    } else {
        ld.d_type = DT_REG;
    }
    ld.d_name[DNAME_LENGTH] = '\0';
    memcpy((char *) file_shm + file_size, &ld, sizeof(ld));
    off64_t base_offset = file_size;

    c_file.insert_sector(base_offset, data_size);
    ++c_file.n_files;
    int pid           = pids[tid];
    writers[pid][dir] = true;

    if (c_file.n_files == c_file.n_files_expected) {
        c_file.complete = true;
        reply_remote_stats(dir);
    }

    std::string_view mode = c_file.get_mode();
    if (mode == CAPIO_FILE_MODE_NOUPDATE) {
        handle_pending_remote_reads(dir, data_size, c_file.complete);
    }
}

void update_dir(int tid, const std::string &file_path, int rank) {
    START_LOG(tid, "call(file_path=%s, rank=%d)", file_path.c_str(), rank);
    std::string dir    = get_parent_dir_path(file_path);
    Capio_file &c_file = get_capio_file(dir.c_str());
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(rank, dir, tid);
    }
    write_entry_dir(tid, file_path, dir, 0);
}

off64_t create_dir(int tid, const char *pathname, int rank, bool root_dir) {
    START_LOG(tid, "call(pathname=%s, rank=%d, root_dir=%s)", pathname, rank,
              root_dir ? "true" : "false");

    if (!get_file_location_opt(pathname)) {
        Capio_file &c_file = create_capio_file(pathname, true, DIR_INITIAL_SIZE);
        if (c_file.first_write) {
            c_file.first_write = false;
            // TODO: it works only if there is one prod per file
            if (root_dir) {
                add_file_location(pathname, node_name, -1);
            } else {
                write_file_location(rank, pathname, tid);
                update_dir(tid, pathname, rank);
            }
            write_entry_dir(tid, pathname, pathname, 1);
            std::string parent_dir = get_parent_dir_path(pathname);
            write_entry_dir(tid, parent_dir, pathname, 2);
        }
        return 0;
    } else {
        return 1;
    }
}

#endif // CAPIO_UTIL_FILESYS_HPP
