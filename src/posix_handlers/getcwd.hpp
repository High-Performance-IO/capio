#ifndef CAPIO_GETCWD_HPP
#define CAPIO_GETCWD_HPP


char *capio_getcw(char *buf, size_t size ) {
    const char *c_current_dir = current_dir->c_str();
    if ((current_dir->length() + 1) * sizeof(char) > size) {
        errno = ERANGE;
        return NULL;
    } else {
        strcpy(buf, c_current_dir);
        CAPIO_DBG("getcw current_dir : %s\n", c_current_dir);
        return buf;
    }

}

int getcw_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long my_tid){

    char *buf = reinterpret_cast<char *>(arg0);
    size_t size = static_cast<size_t>(arg1);
    char *rescw = capio_getcw(buf, size);
    if (rescw == NULL) {
        *result = -errno;
    }
   return 0;
}

#endif //CAPIO_GETCWD_HPP
