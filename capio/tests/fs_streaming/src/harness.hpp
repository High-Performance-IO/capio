#ifndef CAPIO_TESTS_FS_STREAMING_HARNESS_HPP
#define CAPIO_TESTS_FS_STREAMING_HARNESS_HPP

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

/// @brief A file or directory of the CAPIO-CL configuration of a test
struct Stream {
    std::string name;
    std::string rule = "on_close";
    std::string mode = "update";
    long n_files     = -1;
    bool produced    = true; ///< Whether app "prod" produces it. App "cons" always consumes it.
};

struct Result {
    bool finished;
    std::string output;
};

/**
 * One server for each test, in a new CAPIO_DIR. Apps are capio_fs_streaming_app, preloaded with
 * the FS variant and started in CAPIO_DIR.
 */
class FsStreamingTest : public testing::Test {
  protected:
    std::filesystem::path root, work;
    std::string workflow;
    pid_t server = -1;
    int n_apps   = 0;

    struct App {
        pid_t pid;
        std::filesystem::path output;
    };
    std::vector<pid_t> running;

    static std::string setting(const char *name, const char *otherwise) {
        const char *value = std::getenv(name);
        return value != nullptr ? value : otherwise;
    }

    static std::string slurp(const std::filesystem::path &path) {
        std::ostringstream content;
        content << std::ifstream(path).rdbuf();
        return content.str();
    }

    void SetUp() override {
        auto name = (std::filesystem::temp_directory_path() / "capio_fs_XXXXXX").string();
        ASSERT_NE(mkdtemp(name.data()), nullptr);
        root     = std::filesystem::canonical(name);
        work     = root / "work";
        workflow = root.filename().string();
        std::filesystem::create_directory(work);
    }

    void TearDown() override {
        for (const auto pid : running) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        stopServer();
        if (HasFailure()) {
            std::cout << "server log:\n" << slurp(root / "server.log") << std::endl;
        }
        std::filesystem::remove_all(root);
    }

    static bool reap(const pid_t pid, const std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (waitpid(pid, nullptr, WNOHANG) == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    pid_t spawn(const std::vector<std::string> &command, const std::vector<std::string> &env,
                const std::filesystem::path &output, const std::filesystem::path &cwd) const {
        const pid_t pid = fork();
        if (pid == 0) {
            for (const auto &entry : env) {
                const auto equals = entry.find('=');
                setenv(entry.substr(0, equals).c_str(), entry.substr(equals + 1).c_str(), 1);
            }
            unsetenv("OLDPWD");
            const int out = open(output.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            dup2(out, STDOUT_FILENO);
            dup2(out, STDERR_FILENO);
            close(out);
            if (chdir(cwd.c_str()) != 0) {
                _exit(126);
            }
            std::vector<char *> args;
            for (const auto &argument : command) {
                args.push_back(const_cast<char *>(argument.c_str()));
            }
            args.push_back(nullptr);
            execvp(args[0], args.data());
            _exit(127);
        }
        return pid;
    }

    void stopServer() {
        if (server > 0) {
            kill(server, SIGTERM);
            if (!reap(server, std::chrono::seconds(5))) {
                kill(server, SIGKILL);
                waitpid(server, nullptr, 0);
            }
            server = -1;
        }
        // What killed clients left in shared memory, and a server built for coverage, which skips
        // its teardown: the canary among it would stop the next server of the workflow
        std::error_code error;
        for (const auto &entry : std::filesystem::directory_iterator("/dev/shm", error)) {
            if (entry.path().filename().string().find(workflow) != std::string::npos) {
                std::filesystem::remove(entry.path(), error);
            }
        }
    }

    /// @brief Start the server with @p streams, or with --no-config if there are none
    void startServer(const std::vector<Stream> &streams) {
        std::vector<std::string> command{setting("CAPIO_SERVER_PATH", "capio_server")};
        if (streams.empty()) {
            command.emplace_back("--no-config");
        } else {
            std::string outputs, inputs, rules;
            for (const auto &stream : streams) {
                const auto path  = "\"" + (work / stream.name).string() + "\"";
                const auto comma = inputs.empty() ? "" : ",";
                inputs += comma + path;
                if (stream.produced) {
                    outputs += (outputs.empty() ? "" : ",") + path;
                }
                rules += comma;
                rules += stream.rule == "on_n_files" ? R"({"dirname":[)" : R"({"name":[)";
                rules += path + R"(],"committed":")" + stream.rule + R"(","mode":")" + stream.mode +
                         "\"";
                if (stream.n_files >= 0) {
                    rules += R"(,"n_files":)" + std::to_string(stream.n_files);
                }
                rules += "}";
            }
            std::ofstream(root / "config.json")
                << R"({"name":")" << workflow
                << R"(","IO_Graph":[{"name":"prod","input_stream":[],)"
                << R"("output_stream":[)" << outputs << R"(],"streaming":[)" << rules
                << R"(]},{"name":"cons","input_stream":[)" << inputs
                << R"(],"output_stream":[],"streaming":[)" << rules << "]}]}";
            command.emplace_back("-c");
            command.push_back((root / "config.json").string());
        }
        server =
            spawn(command,
                  {"CAPIO_DIR=" + work.string(), "CAPIO_WORKFLOW_NAME=" + workflow, "LD_PRELOAD="},
                  root / "server.log", root);
        for (int i = 0; i < 1000; ++i) {
            if (slurp(root / "server.log").find("successfully started") != std::string::npos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FAIL() << "the server did not start";
    }

    /// @brief Start an app named @p name, or with no CAPIO_APP_NAME if it is empty
    App start(const std::string &name, const std::vector<std::string> &operations) {
        std::vector<std::string> command{setting("CAPIO_FS_APP_PATH", "capio_fs_streaming_app")};
        command.insert(command.end(), operations.begin(), operations.end());
        std::vector<std::string> env{
            "CAPIO_DIR=" + work.string(), "CAPIO_WORKFLOW_NAME=" + workflow,
            "LD_PRELOAD=" + setting("CAPIO_FS_LIBRARY", "libcapio_posix_fs.so")};
        if (!name.empty()) {
            env.push_back("CAPIO_APP_NAME=" + name);
        }
        App app{0, root / ("app" + std::to_string(n_apps++))};
        app.pid = spawn(command, env, app.output, work);
        running.push_back(app.pid);
        return app;
    }

    /// @brief Wait for @p app to end, for at most a generous time in case it hangs
    Result finish(const App &app) {
        const bool finished = reap(app.pid, std::chrono::seconds(30));
        if (finished) {
            running.erase(std::find(running.begin(), running.end(), app.pid));
        }
        return {finished, slurp(app.output)};
    }

    Result run(const std::string &name, const std::vector<std::string> &operations) {
        return finish(start(name, operations));
    }
};

#define EXPECT_OUTPUT(result, expected)                                                            \
    do {                                                                                           \
        const Result result_ = (result);                                                           \
        EXPECT_TRUE(result_.finished) << "the app is still waiting";                               \
        EXPECT_EQ(result_.output, expected);                                                       \
    } while (false)

#endif // CAPIO_TESTS_FS_STREAMING_HARNESS_HPP
