#ifndef CAPIO_EXECVE_HPP
#define CAPIO_EXECVE_HPP
int execve_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    create_snapshot(files, capio_files_descriptors, my_tid);
    return 1;
}
#endif //CAPIO_EXECVE_HPP
