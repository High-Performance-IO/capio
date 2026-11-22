/*
 * The application of the FS streaming tests. Its arguments are operations, run in order on a
 * current descriptor, e.g. "create:out" "write:AAAA" "sleep:500" "close". A failed operation
 * prints "<operation>=<errno name>" and the next one runs.
 */
#include <atomic>
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
#include <spawn.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/xattr.h>
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

bool print_bytes(const char *bytes, const ssize_t size) {
    if (size < 0) {
        return false;
    }
    print(std::string(bytes, size));
    return true;
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
        {"cloexec", [](const std::string &) { return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0; }},
        {"replace",
         [](const std::string &) {
             const int null_fd = ::open("/dev/null", O_RDONLY);
             return dup2(null_fd, fd) == fd && ::close(null_fd) == 0;
         }},
        {"close_range",
         [](const std::string &) { return syscall(SYS_close_range, fd, fd, 0) == 0; }},
        {"exec",
         [](const std::string &a) {
             execlp("sleep", "sleep", a.c_str(), nullptr);
             return false;
         }},
        {"exec_missing",
         [](const std::string &) {
             execl("/no/such/program", "program", nullptr);
             return errno == ENOENT;
         }},
        {"background",
         [](const std::string &a) {
             std::thread([a] {
                 run(a);
                 sleep(60);
             }).detach();
             return true;
         }},
        {"rename",
         [](const std::string &a) {
             const auto to = a.find('>');
             return ::rename(a.substr(0, to).c_str(), a.substr(to + 1).c_str()) == 0;
         }},
        {"access", [](const std::string &a) { return ::access(a.c_str(), F_OK) == 0; }},
        {"pread",
         [](const std::string &) {
             char buffer[8];
             const ssize_t count = pread(fd, buffer, sizeof(buffer), 0);
             if (count > 0) {
                 print(std::string(buffer, count));
             }
             return count >= 0;
         }},
        {"preadv",
         [](const std::string &) {
             char buffer[8];
             iovec part{buffer, sizeof(buffer)};
             const ssize_t count = preadv(fd, &part, 1, 0);
             if (count > 0) {
                 print(std::string(buffer, count));
             }
             return count >= 0;
         }},
        {"mmap",
         [](const std::string &) {
             // Without fstat, which would wait on its own in update mode
             auto *data = static_cast<char *>(mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE, fd, 0));
             if (data == MAP_FAILED) {
                 return false;
             }
             print(std::string(data, strnlen(data, 4096)));
             return true;
         }},
        {"seek_end",
         [](const std::string &) {
             const off_t end = lseek(fd, 0, SEEK_END);
             print(std::to_string(end));
             return end >= 0;
         }},
        {"seek_data",
         [](const std::string &a) {
             const off_t data = lseek(fd, std::stol(a), SEEK_DATA);
             print(std::to_string(data));
             return data >= 0;
         }},
        {"copy_file_range",
         [](const std::string &a) {
             const int out = ::open(a.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
             ssize_t count;
             while ((count = copy_file_range(fd, nullptr, out, nullptr, 1 << 30, 0)) > 0) {
             }
             ::close(out);
             return count == 0;
         }},
        {"sendfile",
         [](const std::string &a) {
             const int out = ::open(a.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
             ssize_t count;
             while ((count = sendfile(out, fd, nullptr, 1 << 30)) > 0) {
             }
             ::close(out);
             return count == 0;
         }},
        {"splice",
         [](const std::string &) {
             int channel[2];
             if (pipe(channel) != 0) {
                 return false;
             }
             ssize_t count;
             while ((count = splice(fd, nullptr, channel[1], nullptr, 4096, 0)) > 0) {
                 std::string data(count, '\0');
                 if (::read(channel[0], data.data(), count) != count) {
                     return false;
                 }
                 print(data);
             }
             return count == 0;
         }},
        {"cp",
         [](const std::string &a) {
             const auto to = a.find('>');
             execlp("cp", "cp", a.substr(0, to).c_str(), a.substr(to + 1).c_str(), nullptr);
             return false;
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
        {"list_first",
         [](const std::string &a) {
             DIR *dir = opendir(a.c_str());
             if (dir == nullptr) {
                 return false;
             }
             while (const auto *entry = readdir(dir)) {
                 if (entry->d_name[0] != '.') {
                     print(entry->d_name);
                     break;
                 }
             }
             return closedir(dir) == 0;
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
        {"getxattr",
         [](const std::string &a) {
             char value[64];
             const ssize_t size = getxattr(a.c_str(), "user.note", value, sizeof(value));
             return print_bytes(value, size);
         }},
        {"lgetxattr",
         [](const std::string &a) {
             char value[64];
             const ssize_t size = lgetxattr(a.c_str(), "user.note", value, sizeof(value));
             return print_bytes(value, size);
         }},
        {"fgetxattr",
         [](const std::string &) {
             char value[64];
             return print_bytes(value, fgetxattr(fd, "user.note", value, sizeof(value)));
         }},
        {"flistxattr",
         [](const std::string &) {
             char names[512];
             const ssize_t size = flistxattr(fd, names, sizeof(names));
             for (ssize_t at = 0; at < size; at += static_cast<ssize_t>(strlen(names + at)) + 1) {
                 if (strncmp(names + at, "user.", 5) == 0) {
                     print(names + at);
                 }
             }
             return size >= 0;
         }},
        {"hammer",
         [](const std::string &a) {
             struct stat info{};
             for (;;) {
                 ::stat(a.c_str(), &info);
             }
             return true;
         }},
        {"pool",
         [](const std::string &a) {
             const int n_files = std::stoi(a);
             std::atomic<int> next{0}, good{0};
             std::vector<std::thread> threads;
             for (int i = 0; i < 8; ++i) {
                 threads.emplace_back([&] {
                     for (int file; (file = next++) < n_files;) {
                         const auto name  = "pool" + std::to_string(file);
                         const int out    = ::open(name.c_str(), O_CREAT | O_WRONLY, 0644);
                         const bool wrote = ::write(out, "x", 1) == 1;
                         if (::close(out) != 0 || !wrote) {
                             continue;
                         }
                         const int in = ::open(name.c_str(), O_RDONLY);
                         char byte    = 0;
                         good += ::read(in, &byte, 1) == 1 && byte == 'x';
                         ::close(in);
                     }
                 });
             }
             for (auto &thread : threads) {
                 thread.join();
             }
             return good == n_files;
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
