/*
 * The application of the FS streaming tests. Its arguments are operations, run in order on a
 * current descriptor, e.g. "create:out" "write:AAAA" "close". A failed operation
 * prints "<operation>=<errno name>" and the next one runs.
 */
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

using Operation = std::function<bool(const std::string &)>;

const std::map<std::string, Operation> &operations() {
    static const std::map<std::string, Operation> table = {
        {"open", [](const std::string &a) { return open_as(a, O_RDONLY); }},
        {"create", [](const std::string &a) { return open_as(a, O_CREAT | O_WRONLY | O_TRUNC); }},
        {"excl", [](const std::string &a) { return open_as(a, O_CREAT | O_EXCL | O_WRONLY); }},
        {"append", [](const std::string &a) { return open_as(a, O_WRONLY | O_APPEND); }},
        {"rewrite", [](const std::string &a) { return open_as(a, O_WRONLY | O_TRUNC); }},
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
        {"cloexec", [](const std::string &) { return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0; }},
        {"fork_close",
         [](const std::string &) {
             const pid_t child = fork();
             if (child == 0) {
                 _exit(close(fd) == 0 ? 0 : 1);
             }
             int status = 0;
             return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                    WEXITSTATUS(status) == 0;
         }},
        {"replace",
         [](const std::string &) {
             const int null_fd = ::open("/dev/null", O_RDONLY);
             return dup2(null_fd, fd) == fd && ::close(null_fd) == 0;
         }},
        {"close_range",
         [](const std::string &) { return syscall(SYS_close_range, fd, fd, 0) == 0; }},
        {"exec",
         [](const std::string &) {
             execlp("true", "true", nullptr);
             return false;
         }},
        {"exec_missing",
         [](const std::string &) {
             execl("/no/such/program", "program", nullptr);
             return errno == ENOENT;
         }},
        {"seek", [](const std::string &a) { return lseek(fd, std::stol(a), SEEK_SET) >= 0; }},
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
        {"access", [](const std::string &a) { return ::access(a.c_str(), F_OK) == 0; }},
        {"mkdir", [](const std::string &a) { return mkdir(a.c_str(), 0755) == 0; }},
        {"rename",
         [](const std::string &a) {
             const auto to = a.find('>');
             return ::rename(a.substr(0, to).c_str(), a.substr(to + 1).c_str()) == 0;
         }},
        {"pread",
         [](const std::string &) {
             char buffer[8];
             const ssize_t count = pread(fd, buffer, sizeof(buffer), 0);
             if (count > 0) {
                 print(std::string(buffer, count));
             }
             return count >= 0;
         }},
        {"seek", // "end", or "data:<offset>"
         [](const std::string &a) {
             const off_t at =
                 a == "end" ? lseek(fd, 0, SEEK_END) : lseek(fd, std::stol(a.substr(5)), SEEK_DATA);
             print(std::to_string(at));
             return at >= 0;
         }},
        {"copy", // "<method>><path>", with copy_file_range, sendfile or splice (through a pipe)
         [](const std::string &a) {
             const auto to            = a.find('>');
             const std::string method = a.substr(0, to);
             const int out  = ::open(a.substr(to + 1).c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
             int channel[2] = {-1, -1};
             if (method == "splice" && pipe(channel) != 0) {
                 return false;
             }
             ssize_t count;
             do {
                 if (method == "copy_file_range") {
                     count = copy_file_range(fd, nullptr, out, nullptr, 1 << 30, 0);
                 } else if (method == "sendfile") {
                     count = sendfile(out, fd, nullptr, 1 << 30);
                 } else if ((count = splice(fd, nullptr, channel[1], nullptr, 4096, 0)) > 0) {
                     count = splice(channel[0], nullptr, out, nullptr, count, 0);
                 }
             } while (count > 0);
             ::close(out);
             return count == 0;
         }},
        {"list",
         [](const std::string &a) {
             DIR *dir = opendir(a.c_str());
             if (dir == nullptr) {
                 return false;
             }
             std::set<std::string> names;
             while (const auto *entry = readdir(dir)) {
                 if (entry->d_name[0] != '.') {
                     names.insert(entry->d_name);
                 }
             }
             closedir(dir);
             for (const auto &name : names) {
                 print(name + ",");
             }
             return true;
         }},
        {"list_twice",
         [](const std::string &a) {
             DIR *dir = opendir(a.c_str());
             if (dir == nullptr) {
                 return false;
             }
             for (int pass = 0; pass < 2; ++pass) {
                 int count = 0;
                 while (const auto *entry = readdir(dir)) {
                     count += entry->d_name[0] != '.';
                 }
                 print(std::to_string(count));
                 rewinddir(dir);
             }
             return closedir(dir) == 0;
         }},
        {"list_seek",
         [](const std::string &a) {
             DIR *dir = opendir(a.c_str());
             if (dir == nullptr || readdir(dir) == nullptr) {
                 return false;
             }
             const long position = telldir(dir);
             const auto *second  = readdir(dir);
             if (second == nullptr) {
                 return false;
             }
             const std::string expected = second->d_name;
             seekdir(dir, position);
             const auto *again = readdir(dir);
             print(again != nullptr && expected == again->d_name ? "same" : "different");
             return closedir(dir) == 0;
         }},
        {"scandir",
         [](const std::string &a) {
             struct dirent **entries;
             const int count = ::scandir(a.c_str(), &entries, nullptr, alphasort);
             if (count < 0) {
                 return false;
             }
             for (int i = 0; i < count; ++i) {
                 if (entries[i]->d_name[0] != '.') {
                     print(std::string(entries[i]->d_name) + ",");
                 }
                 free(entries[i]);
             }
             free(entries);
             return true;
         }},
        {"thread",
         [](const std::string &a) {
             std::thread thread([&a] { run(a); });
             thread.join();
             return true;
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
