#include <pwd.h>

#include "common/syscall.hpp"

#include "utils/clone.hpp"
#include "utils/filesystem.hpp"
#include "utils/snapshot.hpp"

#include "posix/handlers.hpp"

inline int server_pid = -1;

inline char **build_args() {
    const auto args = static_cast<char **>(malloc(3 * sizeof(uintptr_t)));

    char const *command = std::getenv("CAPIO_SERVER_PATH");
    if (command == nullptr) {
        command = "capio_server";
    }

    args[0] = strdup(command);
    args[1] = strdup("--no-config");
    args[2] = static_cast<char *>(nullptr);

    return args;
}

inline char **build_env() {
    const char *home = getenv("HOME");

    if (!home) {
        if (passwd *pw = getpwuid(getuid())) {
            home = pw->pw_dir;
        }
    }

    // 2 variables + NULL
    auto env = static_cast<char **>(malloc(4 * sizeof(char *)));

    env[0] = strdup("CAPIO_DIR=/tmp");
    env[1] = strdup("CAPIO_LOG_LEVEL=-1");

    if (home) {
        std::string home_var = std::string("HOME=") + home;
        env[2]               = strdup(home_var.c_str());
        env[3]               = nullptr;
    } else {
        // extremely unlikely, but keep env valid
        env[2] = nullptr;
    }

    return env;
}

class CapioServerEnvironment : public testing::Test {

  protected:
    static void SetUpTestSuite() {

        char **args = build_args();
        char **envp = build_env();
        if (server_pid < 0) {
            const auto server_path = std::getenv("CAPIO_SERVER_PATH");
            ASSERT_GE(server_pid = fork(), 0);
            ASSERT_NE(server_path, nullptr);
            if (server_pid == 0) {
                execvpe(server_path, args, envp);
                _exit(127);
            }
            sleep(5);
        }
    }

    static void TearDownTestSuite() {
        if (server_pid > 0) {
            kill(server_pid, SIGTERM);
            waitpid(server_pid, nullptr, 0);
            server_pid = -1;
        }
    }
};

TEST_F(CapioServerEnvironment, testReadWrite1GBFileBy4kBuffer) {

    putenv("CAPIO_DIR=/tmp");

    const auto tid       = gettid();
    const auto wf_name   = get_capio_workflow_name();
    const auto lines     = CAPIO_CACHE_LINES_DEFAULT;
    const auto line_size = CAPIO_CACHE_LINE_SIZE_DEFAULT;
    long result;

    constexpr uint64_t FILE_SIZE = 1024 * 1024 * 1024;

    char pathname[] = "/tmp/data.dat";

    const auto write_buffer = new char[FILE_SIZE]; // 1GB data
    const auto read_buffer  = new char[FILE_SIZE]; // 1GB data

    // fill write_buffer and read buffer
    for (auto i = 0; i < FILE_SIZE; i++) {
        write_buffer[i] = '1'; // init all data to 1
        read_buffer[i]  = '0'; // init all data to 0, so that afterward it will become 1
    }

    init_client(tid);
    init_filesystem();
    initialize_new_thread();

    long fd;

    open_handler(reinterpret_cast<long>(pathname), O_RDWR | O_CREAT, 0, 0, 0, 0, &fd);
    EXPECT_GT(fd, 3);
    write_handler(fd, reinterpret_cast<long>(write_buffer), FILE_SIZE, 0, 0, 0, &result);
    close_handler(fd, 0, 0, 0, 0, 0, &result);

    open_handler(reinterpret_cast<long>(pathname), O_RDWR, 0, 0, 0, 0, &fd);
    EXPECT_GT(fd, 3);

    read_handler(fd, reinterpret_cast<long>(read_buffer), FILE_SIZE, 0, 0, 0, &result);
    close_handler(fd, 0, 0, 0, 0, 0, &result);

    for (auto i = 0; i < FILE_SIZE; i++) {
        EXPECT_EQ(read_buffer[i], write_buffer[i]);
    }

    delete[] write_buffer;
    delete[] read_buffer;
}