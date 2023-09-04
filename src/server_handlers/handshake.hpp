#ifndef CAPIO_HANDSHAKE_HPP
#define CAPIO_HANDSHAKE_HPP
void handle_handshake(const char* str, bool app_name_defined) {
    int tid, pid;
    char app_name[1024];

    if (app_name_defined) {
        sscanf(str, "hand %d %d %s", &tid, &pid, app_name);
        apps[tid] = app_name;
    }
    else
        sscanf(str, "hans %d %d", &tid, &pid);
    pids[tid] = pid;
    init_process(tid);
}
#endif //CAPIO_HANDSHAKE_HPP
