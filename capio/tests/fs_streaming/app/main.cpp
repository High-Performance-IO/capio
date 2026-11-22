/*
 * The application of the FS streaming tests. Its arguments are operations, run in order on a
 * current descriptor, e.g. "create:out" "write:AAAA" "sleep:500" "close". A failed operation
 * prints "<operation>=<errno name>" and the next one runs.
 */
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int fd = -1, previous_fd = -1;

void run(const std::string &argument);

void print(const std::string &text) {
    fputs(text.c_str(), stdout);
    fflush(stdout);
}

const char *error_name(const int error) {
    switch (error) {
    case EBADF:
        return "EBADF";
    case EEXIST:
        return "EEXIST";
    case EINVAL:
        return "EINVAL";
    case ENOENT:
        return "ENOENT";
    default:
        return strerror(error);
    }
}

bool read_all() {
    char buffer[4096];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        print(std::string(buffer, count));
    }
    return count == 0;
}

bool open_as(const std::string &path, const int flags) {
    fd = open(path.c_str(), flags, 0644);
    return fd >= 0;
}

bool wait_child(const pid_t pid) {
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid;
}

using Operation = std::function<bool(const std::string &)>;

const std::map<std::string, Operation> &operations() {
    static const std::map<std::string, Operation> table = {
        {"open", [](const std::string &a) { return open_as(a, O_RDONLY); }},
        {"create", [](const std::string &a) { return open_as(a, O_CREAT | O_WRONLY | O_TRUNC); }},
        {"excl", [](const std::string &a) { return open_as(a, O_CREAT | O_EXCL | O_WRONLY); }},
        {"append", [](const std::string &a) { return open_as(a, O_WRONLY | O_APPEND); }},
        {"write",
         [](const std::string &a) {
             return write(fd, a.data(), a.size()) == static_cast<ssize_t>(a.size());
         }},
        {"read", [](const std::string &) { return read_all(); }},
        {"close", [](const std::string &) { return close(fd) == 0; }},
        {"close_previous", [](const std::string &) { return close(previous_fd) == 0; }},
        {"dup",
         [](const std::string &) {
             previous_fd = fd;
             fd          = dup(fd);
             return fd >= 0;
         }},
        {"seek", [](const std::string &a) { return lseek(fd, std::stol(a), SEEK_SET) >= 0; }},
        {"sleep", [](const std::string &a) { return usleep(std::stoi(a) * 1000) == 0; }},
        {"chdir", [](const std::string &a) { return chdir(a.c_str()) == 0; }},
        {"mkdir", [](const std::string &a) { return mkdir(a.c_str(), 0755) == 0; }},
        {"unlink", [](const std::string &a) { return unlink(a.c_str()) == 0; }},
        {"print",
         [](const std::string &a) {
             print(a);
             return true;
         }},
        {"stat",
         [](const std::string &a) {
             struct stat info{};
             if (stat(a.c_str(), &info) != 0) {
                 return false;
             }
             print(S_ISDIR(info.st_mode) ? "dir" : std::to_string(info.st_size));
             return true;
         }},
        {"thread",
         [](const std::string &a) {
             std::thread thread([&a] { run(a); });
             thread.join();
             return true;
         }},
        {"fork",
         [](const std::string &a) {
             const pid_t pid = fork();
             if (pid == 0) {
                 run(a);
                 _exit(0);
             }
             return wait_child(pid);
         }},
        {"spawn",
         [](const std::string &) {
             pid_t pid;
             char name[]  = "true";
             char *args[] = {name, nullptr};
             return posix_spawnp(&pid, name, nullptr, nullptr, args, environ) == 0 &&
                    wait_child(pid);
         }},
    };
    return table;
}

void run(const std::string &argument) {
    const auto separator = argument.find(':');
    const auto name      = argument.substr(0, separator);
    const auto value =
        separator == std::string::npos ? std::string{} : argument.substr(separator + 1);
    const auto found = operations().find(name);
    if (found == operations().end()) {
        print("unknown operation " + name);
        _exit(64);
    }
    if (!found->second(value)) {
        print(name + "=" + error_name(errno));
    }
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        run(argv[i]);
    }
    return 0;
}
