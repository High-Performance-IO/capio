#ifndef CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

#include "remote/backend.hpp"

std::vector<std::string> *files_available(const std::string &prefix, const std::string &app_name,
                                          const std::string &path, int nfiles) {
    START_LOG(gettid(), "call(prefix=%s, app_name=%s, path=%s, nfiles=%d)", prefix.c_str(),
              app_name.c_str(), path.c_str(), nfiles);

    auto files_to_send                     = new std::vector<std::string>;
    std::unordered_set<std::string> &files = files_sent[app_name];

    auto capio_file_opt = get_capio_file_opt(path);
    if (capio_file_opt) {
        CapioFile &c_file = capio_file_opt->get();
        if (c_file.is_complete()) {
            files_to_send->emplace_back(path);
            files.insert(path);
        }
    } else {
        return files_to_send;
    }

    for (auto &file_path : get_capio_file_paths()) { // DATA RACE on files_metadata
        auto file_location_opt = get_file_location_opt(file_path);

        if (files.find(file_path) == files.end() && file_location_opt &&
            strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 &&
            file_path.native().compare(0, prefix.length(), prefix) == 0) {

            CapioFile &c_file = get_capio_file(file_path);
            if (c_file.is_complete() && !c_file.is_dir()) {
                files_to_send->emplace_back(file_path);
                files.insert(file_path);
            }
        }
    }
    return files_to_send;
}

void wait_for_data(const std::filesystem::path &path, off64_t offset, int dest, off64_t nbytes,
                   sem_t *data_is_available) {
    START_LOG(gettid(), "call(path=%s, offset=%ld, dest=%d, nbytes=%ld)", path.c_str(), offset,
              dest, nbytes);

    SEM_WAIT_CHECK(data_is_available, "data_is_available");
    backend->serve_remote_read(path, dest, offset, nbytes, get_capio_file(path).is_complete());

    delete data_is_available;
}

void wait_for_files_batch(const std::filesystem::path &prefix, std::vector<std::string> *files_path,
                          int nfiles, int dest, sem_t *n_files_ready) {
    START_LOG(gettid(), "call(prefix=%s, nfiles=%ld, dest=%d)", prefix.c_str(), nfiles, dest);

    SEM_WAIT_CHECK(n_files_ready, "n_files_ready");
    backend->send_files_batch(prefix, files_path, nfiles, dest);

    delete files_path;
    delete n_files_ready;
}

inline void handle_remote_read_batch(const std::filesystem::path &prefix,
                                     const std::string &app_name, const std::filesystem::path &path,
                                     int dest, int nfiles) {
    START_LOG(gettid(), "call(prefix=%s, app_name=%s, path=%s, dest=%d, nfiles=%ld)",
              prefix.c_str(), path.c_str(), app_name.c_str(), dest, nfiles);

    // FIXME: this assignment always overrides the request parameter, which is never used
    nfiles = find_batch_size(prefix, metadata_conf_globs);

    SEM_WAIT_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
    std::vector<std::string> *files = files_available(prefix, app_name, path, nfiles);
    SEM_POST_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");

    if (files->size() == nfiles) {
        backend->send_files_batch(prefix, files, nfiles, dest);
        delete files;
    } else {
        /*
         * create a thread that waits for the completion of such
         * files and then send those files
         */
        auto sem = new sem_t;
        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init in remote_listener_nreads_req");
        }
        std::thread t(wait_for_files_batch, prefix, files, nfiles, dest, sem);

        SEM_WAIT_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
        clients_remote_pending_nfiles[app_name].emplace_back(prefix, nfiles, dest, files, sem);
        SEM_POST_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
    }
}

inline void handle_remote_read(const std::filesystem::path &path, int dest, off64_t offset,
                               off64_t nbytes) {
    START_LOG(gettid(), "call(path=%s, dest=%d, offset=%ld, nbytes=%ld)", path.c_str(), dest,
              offset, nbytes);

    CapioFile &c_file   = get_capio_file(path);
    bool data_available = (offset + nbytes <= c_file.get_stored_size());
    if (c_file.is_complete() ||
        (c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE && data_available)) {
        backend->serve_remote_read(path, dest, offset, nbytes, c_file.is_complete());
    } else {
        auto sem = new sem_t;

        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init sem");
        }
        clients_remote_pending_reads[path].emplace_back(offset, nbytes, sem);
        std::thread t(wait_for_data, path, offset, dest, nbytes, sem);
        t.detach();
    }
}

void remote_read_batch_handler(const RemoteRequest &request) {
    int dest = request.get_source();
    int nfiles;
    char prefix[PATH_MAX];
    char app_name[512];
    char path[PATH_MAX];
    sscanf(request.get_content(), "%d %s %s %s", &nfiles, app_name, prefix, path);
    handle_remote_read_batch(prefix, app_name, path, dest, nfiles);
}

void remote_read_handler(const RemoteRequest &request) {
    char path[PATH_MAX];
    int dest;
    off64_t offset, nbytes;
    sscanf(request.get_content(), "%s %d %ld %ld", path, &dest, &offset, &nbytes);
    handle_remote_read(path, dest, offset, nbytes);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP
