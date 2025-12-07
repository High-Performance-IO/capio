#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP
extern ClientManager *client_manager;

void mkdir_handler(const char *const str) {
    pid_t tid;
    char pathname[PATH_MAX];
    sscanf(str, "%d %s", &tid, pathname);
    client_manager->reply(tid, create_dir(tid, pathname));
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP
