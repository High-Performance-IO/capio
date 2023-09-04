#ifndef CAPIO_MKDIR_HPP
#define CAPIO_MKDIR_HPP


int add_mkdir_request(std::string pathname,long my_tid) {
    char c_str[256];

    sprintf(c_str, "mkdi %ld %s", my_tid, pathname.c_str());
    buf_requests->write(c_str, 256 * sizeof(char));
    off64_t res_tmp;
    (*bufs_response)[my_tid]->read(&res_tmp);

    if (res_tmp == 1)
        return -1;
    else
        return 0;
}

int request_mkdir(std::string path_to_check,long tid){
    int res;
    auto res_mismatch = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());
    if (res_mismatch.first == capio_dir->end()) {
        if (capio_dir->size() == path_to_check.size()) {
            return -2;

        } else {
            if (capio_files_paths->find(path_to_check) != capio_files_paths->end()) {
                errno = EEXIST;
                return -1;
            }
            res = add_mkdir_request(path_to_check, tid);
            if (res == 0)
                capio_files_paths->insert(path_to_check);

            CAPIO_DBG("capio_mkdir returning %d\n", res);

            return res;
        }
    } else {

        CAPIO_DBG("capio_mkdir returning -2\n");
        return -2;
    }
}


int capio_mkdir(const char *pathname,mode_t mode,long tid) {
    std::string path_to_check;
    if (is_absolute(pathname)) {
        path_to_check = pathname;

        CAPIO_DBG("capio_mkdir absolute %s\n", path_to_check.c_str());
    } else {
        path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
        if (path_to_check.length() == 0)
            return -2;

        CAPIO_DBG("capio_mkdir relative path%s\n", path_to_check.c_str());

    }
    return request_mkdir(path_to_check, tid);

}


int mkdir_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg0);
    int res = capio_mkdir(pathname, static_cast<mode_t>(arg1), my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_MKDIR_HPP
