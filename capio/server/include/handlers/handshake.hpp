#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP


void handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    client_manager->register_client(CAPIO_DEFAULT_APP_NAME, tid);
}

void handshake_named_handler(const char *const str) {
    int tid, pid;
    char app_name[1024];
    sscanf(str, "%d %d %s", &tid, &pid, app_name);
    client_manager->register_client(app_name, tid);
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
