#ifndef CAPIO_EXIT_GROUP_HPP
#define CAPIO_EXIT_GROUP_HPP

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
*/

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    char c_str[256];
    sprintf(c_str, "exig %ld", my_tid);

    CAPIO_DBG("capio exit group captured%d\n", my_tid);

    buf_requests->write(c_str, 256 * sizeof(char));

    CAPIO_DBG("capio exit group terminated%d\n", my_tid);

    return 1;
}
#endif //CAPIO_EXIT_GROUP_HPP
