#include <gtest/gtest.h>

char **build_args() {
    char **args = (char **) malloc(3 * sizeof(uintptr_t));

    char const *command = std::getenv("CAPIO_SERVER_PATH");
    if (command == nullptr) {
        command = "/home/marco/Desktop/capio/cmake-build-debug/src/server/capio_server";
    }

    args[0] = strdup(command);
    args[1] = strdup("--no-config");
    args[2] = (char *) nullptr;

    return args;
}

char **build_env(char **envp) {
    std::vector<int> vars;
    for (int i = 0; envp[i] != nullptr; i++) {
        const std::string_view var(envp[i]);
        const std::string_view key = var.substr(0, var.find('='));
        if (key != "LD_PRELOAD") {
            vars.emplace_back(i);
        }
    }

    char **cleaned_env = (char **) malloc((vars.size() + 2) * sizeof(uintptr_t));
    for (int i = 0; i < vars.size(); i++) {
        cleaned_env[i] = strdup(envp[i]);
    }
    cleaned_env[vars.size()]     = strdup("LD_PRELOAD=");
    cleaned_env[vars.size() + 1] = (char *) nullptr;

    return cleaned_env;
}

class CapioServerEnvironment : public testing::Environment {
  private:
    char **args;
    char **envp;
    int server_pid;

  public:
    explicit CapioServerEnvironment(char **envp)
        : args(build_args()), envp(build_env(envp)), server_pid(-1){};

    ~CapioServerEnvironment() override {
        for (int i = 0; args[i] != nullptr; i++) {
            free(args[i]);
        }
        free(args);
        for (int i = 0; envp[i] != nullptr; i++) {
            free(envp[i]);
        }
        free(envp);
    };

    void SetUp() override {
        if (server_pid < 0) {
            ASSERT_NE(std::getenv("CAPIO_DIR"), nullptr);
            ASSERT_GE(server_pid = fork(), 0);
            if (server_pid == 0) {
                execvpe(args[0], args, envp);
                _exit(127);
            } else {
                sleep(5);
            }
        }
    }

    void TearDown() override {
        if (server_pid > 0) {
            kill(server_pid, SIGTERM);
            waitpid(server_pid, nullptr, 0);
            server_pid = -1;
        }
    }
};

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new CapioServerEnvironment(envp));

    return RUN_ALL_TESTS();
}
