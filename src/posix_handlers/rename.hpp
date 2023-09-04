#ifndef CAPIO_RENAME_HPP
#define CAPIO_RENAME_HPP

void copy_file(std::string path_1, std::string path_2) {
    FILE *fp_1 = fopen(path_1.c_str(), "r");
    if (fp_1 == NULL)
        err_exit("fopen fp_1 in copy_file", "copy_file");
    FILE *fp_2 = fopen(path_2.c_str(), "w");
    if (fp_2 == NULL)
        err_exit("fopen fp_2 in copy_file", "copy_file");
    char buf[1024];
    int res;
    while ((res = fread(buf, sizeof(char), 1024, fp_1)) == 1024) {
        fwrite(buf, sizeof(char), 1024, fp_2);
    }
    if (res != 0) {
        fwrite(buf, sizeof(char), res, fp_2);
    }
    if (fclose(fp_1) == EOF)
        err_exit("fclose fp_1", "copy_file");

    if (fclose(fp_2) == EOF)
        err_exit("fclose fp_2", "copy_file");
}

void mv_file_capio(std::string oldpath_abs,std::string newpath_abs,long tid) {
    copy_file(oldpath_abs, newpath_abs);

    char c_str[256];
    sprintf(c_str, "unlk %ld %s", tid, oldpath_abs.c_str());
    buf_requests->write(c_str, 256 * sizeof(char));
    off64_t res_unlink;
    (*bufs_response)[tid]->read(&res_unlink);
}

void copy_inside_capio(std::string oldpath_abs, std::string newpath_abs) {
    copy_file(oldpath_abs, newpath_abs);
}


int capio_rename(const char *oldpath,const char *newpath,long tid) {
    std::string oldpath_abs, newpath_abs;

    CAPIO_DBG("rename captured, checking if are CAPIO files...\n");

    if (is_absolute(oldpath)) {
        oldpath_abs = oldpath;
    } else {
        oldpath_abs = create_absolute_path(oldpath, capio_dir, current_dir, stat_enabled);
    }

    bool oldpath_capio = is_capio_path(oldpath_abs, capio_dir);

    if (is_absolute(newpath)) { //TODO: move this control inside create_absolute_path
        newpath_abs = newpath;
    } else {
        newpath_abs = create_absolute_path(newpath, capio_dir, current_dir, stat_enabled);
    }

    bool newpath_capio = is_capio_path(newpath_abs, capio_dir);

    if (is_prefix(oldpath_abs, newpath_abs)) {//TODO: The check is more complex
        errno = EINVAL;
        return -1;
    }

    if (oldpath_capio) {
        if (newpath_capio) {
            CAPIO_DBG("rename capio\n");
            mv_file_capio(oldpath_abs, newpath_abs, tid);
        } else {
            CAPIO_DBG("copy_outside_capio\n");
            mv_file_capio(oldpath_abs, newpath_abs, tid);
        }
    } else {
        if (newpath_capio) {
            CAPIO_DBG("copy_inside_capio\n");
            copy_inside_capio(oldpath_abs, newpath_abs);
        } else { //Both aren't CAPIO paths
            CAPIO_DBG("rename not interessing to CAPIO\n");
            return -2;
        }

    }

    return 0;
}

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *oldpath = reinterpret_cast<const char *>(arg0);
    const char *newpath = reinterpret_cast<const char *>(arg1);
    int res = capio_rename(oldpath, newpath, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_RENAME_HPP
