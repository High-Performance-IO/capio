#ifndef CAPIO_COMMON_HPP
#define CAPIO_COMMON_HPP
void add_dup_request(int old_fd, int new_fd, long tid) {
    char c_str[256];
    sprintf(c_str, "dupp %ld %d %d", tid, old_fd, new_fd);
    buf_requests->write(c_str, 256 * sizeof(char));
}

int capio_file_exists(std::string path, long tid) {
    off64_t res;
    char c_str[256];

    sprintf(c_str, "accs %ld %s", tid, path.c_str());
    buf_requests->write(c_str, 256 * sizeof(char));
    (*bufs_response)[tid]->read(&res);
    return res;
}

int is_capio_file(std::string abs_path, std::string *capio_dir) {
    auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
    if (it.first == capio_dir->end())
        return 0;
    else
        return -1;
}

int capio_unlink_abs(std::string abs_path, long pid) {
    int res;
    auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
    if (it.first == capio_dir->end()) {
        if (capio_dir->size() == abs_path.size()) {
            std::cerr << "ERROR: unlink to the capio_dir " << abs_path << std::endl;
            exit(1);
        }

        char c_str[256];
        sprintf(c_str, "unlk %ld %s", pid, abs_path.c_str());
        buf_requests->write(c_str, 256 * sizeof(char));
        off64_t res_unlink;
        (*bufs_response)[pid]->read(&res_unlink);
        res = res_unlink;
        if (res == -1)
            errno = ENOENT;
    } else {
        res = -2;
    }
    return res;
}



void copy_parent_files() {

    CAPIO_DBG("Im process %d and my parent is %d\n", syscall_no_intercept(SYS_gettid), parent_tid);

    char c_str[256];
    sprintf(c_str, "clon %ld %ld", parent_tid, syscall_no_intercept(SYS_gettid));
    buf_requests->write(c_str, 256 * sizeof(char));
}


/*
 * This function must be called only once
 *
 */

void mtrace_init() {
    int my_tid = syscall_no_intercept(SYS_gettid);
    if (first_call == nullptr)
        first_call = new std::set<int>();
    first_call->insert(my_tid);
    sem_post(sem_first_call);
    if (parent_tid == my_tid) {

        CAPIO_DBG("sem wait parent before %ld %ld\n", parent_tid, my_tid);
        CAPIO_DBG("sem_family %ld\n", sem_family);

        sem_wait(sem_family);

        CAPIO_DBG("sem wait parent after\n");

        sem_post(sem_clone);

        return;
    }
    (*stat_enabled)[my_tid] = false;

    if (capio_files_descriptors == nullptr) {

        CAPIO_DBG("init data_structures\n");

        capio_files_descriptors = new std::unordered_map<int, std::string>;
        capio_files_paths = new std::unordered_set<std::string>;

        files = new std::unordered_map<int, std::tuple<off64_t*, off64_t*, int, int>>;

        int* fd_shm = get_fd_snapshot(my_tid);
        if (fd_shm != nullptr) {
            initialize_from_snapshot(fd_shm, files, capio_files_descriptors, capio_files_paths, my_tid);
        }
        threads_data_bufs = new std::unordered_map<int, std::pair<SPSC_queue<char>*, SPSC_queue<char>*>>;
        std::string shm_name = "capio_write_data_buffer_tid_" + std::to_string(my_tid);
        auto* write_queue = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
        shm_name = "capio_read_data_buffer_tid_" + std::to_string(my_tid);
        auto* read_queue = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
        threads_data_bufs->insert({my_tid, {write_queue, read_queue}});
    }
    char* val;
    if (capio_dir == nullptr) {
        val = getenv("CAPIO_DIR");
        try {
            if (val == NULL) {
                capio_dir = new std::string(std::filesystem::canonical("."));
            }
            else {
                capio_dir = new std::string(std::filesystem::canonical(val));
            }
            current_dir = new std::string(*capio_dir);
        }
        catch (const std::exception& ex) {
            exit(1);
        }
        get_app_name(capio_app_name);
        int res = is_directory(capio_dir->c_str());
        if (res == 0) {
            std::cerr << "dir " << capio_dir << " is not a directory" << std::endl;
            exit(1);
        }
    }
    val = getenv("GW_BATCH");
    if (val != NULL) {
        num_writes_batch = std::stoi(val);
        if (num_writes_batch <= 0) {
            std::cerr << "error: GW_BATCH variable must be >= 0";
            exit(1);
        }
    }

    if (sem_tmp == nullptr)
        sem_tmp = sem_open(("capio_sem_tmp_" + std::to_string(syscall(SYS_gettid))).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1);

    CAPIO_DBG("before second lock\n");

    sem_wait(sem_tmp);

    CAPIO_DBG("after second lock\n");

    if (sems_write == nullptr)
        sems_write = new std::unordered_map<int, sem_t*>();
    sem_t* sem_write = sem_open(("sem_write" + std::to_string(my_tid)).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
    sems_write->insert(std::make_pair(my_tid, sem_write));
    if (buf_requests == nullptr)
        buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 256);
    if (bufs_response == nullptr)
        bufs_response = new std::unordered_map<int, Circular_buffer<off_t>*>();
    Circular_buffer<off_t>* p_buf_response = new Circular_buffer<off_t>("buf_response" + std::to_string(my_tid), 8 * 1024 * 1024, sizeof(off_t));
    bufs_response->insert(std::make_pair(my_tid, p_buf_response));

    sem_post(sem_tmp);
    char c_str[256];
    if (thread_created) {

        CAPIO_DBG("thread created init\n");

        sprintf(c_str, "clon %ld %d", parent_tid, my_tid);
        buf_requests->write(c_str, 256 * sizeof(char));
        sem_post(sem_family);

        CAPIO_DBG("thread created init end\n");
    }

    if (capio_app_name == nullptr)
        sprintf(c_str, "hans %d %d", my_tid, getpid());
    else
        sprintf(c_str, "hand %d %d %s", my_tid, getpid(), capio_app_name->c_str());
    buf_requests->write(c_str, 256 * sizeof(char));
    (*stat_enabled)[my_tid] = true;

    CAPIO_DBG("ending mtrace init %d\n", my_tid);
	CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());

}

off64_t round(off64_t bytes, bool is_getdents64) {
    off64_t res = 0;
    off64_t ld_size;
    if (is_getdents64)
        ld_size = theoretical_size_dirent64;
    else
        ld_size = theoretical_size_dirent;
    while (res + ld_size <= bytes)
        res += ld_size;
    return res;
}

bool is_capio_path(std::string path_to_check, std::string *capio_dir) {
    bool res = false;
    auto mis_res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

    CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());

    if (mis_res.first == capio_dir->end()) {
        if (capio_dir->size() == path_to_check.size()) {
            return -2;

        } else {
            res = true;
        }

    }
    return res;
}

bool is_prefix(std::string path_1, std::string path_2) {
    auto res = std::mismatch(path_1.begin(), path_1.end(), path_2.begin());
    return res.first == path_2.end();
}



#endif //CAPIO_COMMON_HPP
