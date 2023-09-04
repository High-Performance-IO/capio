#ifndef CAPIO_WRITE_HPP
#define CAPIO_WRITE_HPP
//da modifcare con capio_file sia per una normale scrittura sia per quando si fa il batch
void add_write_request(long int my_tid,int fd,off64_t count) {
    char c_str[256];
    long int old_offset = *std::get<0>((*files)[fd]);
    *std::get<0>((*files)[fd]) += count; //works only if there is only one writer at time for each file
    if (actual_num_writes == num_writes_batch) {
        sprintf(c_str, "writ %ld %d %ld %ld", my_tid, fd, old_offset, count);
        buf_requests->write(c_str, 256 * sizeof(char));
        actual_num_writes = 1;
    } else
        ++(actual_num_writes);

}

ssize_t capio_write(int fd,const void *buffer,size_t count,long my_tid) {
    auto it = files->find(fd);
    if (it != files->end()) {
        if (count > SSIZE_MAX) {
            std::cerr << "Capio does not support writes bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;

        add_write_request(my_tid, fd, count_off); //bottleneck
        write_shm((*threads_data_bufs)[my_tid].first, *std::get<0>((*files)[fd]), buffer, count_off);

        return count;
    } else {
        return -2;
    }
}

int write_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,  long my_tid){

    int fd = static_cast<int>(arg0);
    const void *buf = reinterpret_cast<const void *>(arg1);
    size_t count = static_cast<size_t>(arg2);
    (*stat_enabled)[my_tid] = false;
    (*stat_enabled)[my_tid] = true;

    int res = capio_write(fd, buf, count, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_WRITE_HPP
