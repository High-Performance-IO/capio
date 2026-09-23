#include <csignal>
#include <pthread.h>
#include <system_error>
#include <thread>

#include "client-manager/client_manager.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "common/constants.hpp"
#include "common/queue.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

ClientManager::ClientDataBuffers::ClientDataBuffers(const std::string &clientToServerName,
                                                    const std::string &serverToClientName,
                                                    const std::string &wf_name)
    : ClientToServer(clientToServerName, get_cache_lines(), get_cache_line_size(), wf_name),
      ServerToClient(serverToClientName, get_cache_lines(), get_cache_line_size(), wf_name) {}

ClientManager::ClientManager()
    : requests{SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE,
               CapioCLEngine::get().getWorkflowName()} {
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_STATUS, "initialization completed.");
}

void ClientManager::deactivateClient(const std::shared_ptr<ClientState> &state) {
    std::unique_lock lock(state->lifecycle_mutex);
    state->active = false;
    state->lifecycle_cv.wait(lock, [&state] { return state->replies_in_flight == 0; });
}

void ClientManager::reapCompletedWaiters() {
    std::vector<std::thread> completed;
    {
        const std::lock_guard lock(waiter_threads_mutex);
        for (auto waiter = waiter_threads.begin(); waiter != waiter_threads.end();) {
            if (waiter->completed->load() &&
                waiter->thread.get_id() != std::this_thread::get_id()) {
                completed.push_back(std::move(waiter->thread));
                waiter = waiter_threads.erase(waiter);
            } else {
                ++waiter;
            }
        }
    }
    for (auto &thread : completed) {
        thread.join();
    }
}

ClientManager::~ClientManager() {
    shutting_down = true;
    cv_thread_allowed_to_continue.notify_all();

    std::vector<std::shared_ptr<ClientState>> states;
    {
        const std::lock_guard lock(clients_mutex);
        for (const auto &[tid, state] : clients) {
            states.push_back(state);
        }
    }
    for (const auto &state : states) {
        deactivateClient(state);
    }

    std::vector<std::thread> threads;
    {
        const std::lock_guard lock(waiter_threads_mutex);
        for (auto &waiter : waiter_threads) {
            threads.push_back(std::move(waiter.thread));
        }
        waiter_threads.clear();
    }
    for (auto &thread : threads) {
        if (thread.joinable()) {
            if (thread.get_id() == std::this_thread::get_id()) {
                std::terminate();
            }
            thread.join();
        }
    }
    {
        const std::lock_guard lock(clients_mutex);
        clients.clear();
    }
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "teardown completed.");
}

void ClientManager::registerClient(pid_t tid, const std::string &app_name, const bool wait) {
    START_LOG(gettid(), "call(tid=%ld, app_name=%s)", tid, app_name.c_str());

    std::shared_ptr<ClientState> previous;
    {
        const std::lock_guard lock(clients_mutex);
        if (const auto client = clients.find(tid); client != clients.end()) {
            previous = client->second;
            clients.erase(client);
        }
    }
    if (previous != nullptr) {
        deactivateClient(previous);
        cv_thread_allowed_to_continue.notify_all();
    }

    data_buffers.try_emplace(tid, SHM_SPSC_PREFIX_WRITE + std::to_string(tid),
                             SHM_SPSC_PREFIX_READ + std::to_string(tid),
                             CapioCLEngine::get().getWorkflowName());
    files_created_by_producer.emplace(tid, std::initializer_list<std::string>{});
    files_created_by_app_name.emplace(app_name, std::initializer_list<std::string>{});

    auto state      = std::make_shared<ClientState>();
    state->response = std::make_shared<CircularBuffer<off64_t>>(
        SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT, sizeof(off_t),
        CapioCLEngine::get().getWorkflowName());
    {
        const std::lock_guard lock(clients_mutex);
        state->generation = ++next_generation;
        app_names[tid] = app_name;
        clients[tid] = std::move(state);
    }

    DBG(tid, [](const int tid_app, const std::string &app_name_) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Registered PID %d with app name %s", tid_app,
                         app_name_.c_str());
    }(tid, app_name));

    if (wait) {
        spawnWaiter(tid, [&, target_tid = tid](const ClientToken &client) {
            std::unique_lock<std::mutex> lock(mutex_thread_allowed_to_continue);
            cv_thread_allowed_to_continue.wait(lock, [&, target_tid]() {
                return shutting_down ||
                       !isClientActive(client) ||
                       std::find(thread_allowed_to_continue.begin(),
                                 thread_allowed_to_continue.end(),
                                 target_tid) != thread_allowed_to_continue.end();
            });
            const auto it = std::find(thread_allowed_to_continue.begin(),
                                      thread_allowed_to_continue.end(), target_tid);
            if (it != thread_allowed_to_continue.end()) {
                thread_allowed_to_continue.erase(it);
            }
            if (shutting_down || !isClientActive(client)) {
                return;
            }
            tryReply(client, 1);
        });
    }
}

void ClientManager::unlockClonedChild(const pid_t tid) {
    {
        std::lock_guard lock(mutex_thread_allowed_to_continue);
        thread_allowed_to_continue.push_back(tid);
        cv_thread_allowed_to_continue.notify_all();
    }
}

void ClientManager::removeClient(const pid_t tid) {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    std::shared_ptr<ClientState> state;
    std::string app_name = default_app_name;
    {
        const std::lock_guard lock(clients_mutex);
        if (const auto client = clients.find(tid); client != clients.end()) {
            state = client->second;
            clients.erase(client);
        }
        if (const auto app = app_names.find(tid); app != app_names.end()) {
            app_name = app->second;
            app_names.erase(app);
        }
    }
    if (state != nullptr) {
        deactivateClient(state);
        cv_thread_allowed_to_continue.notify_all();
    }
    if (const auto it_resp = data_buffers.find(tid); it_resp != data_buffers.end()) {
        data_buffers.erase(it_resp);
    }
    files_created_by_producer.erase(tid);
    files_created_by_app_name.erase(app_name);

    DBG(tid, [&](const int tid_app, const std::string &app_name) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "Removed PID %d with app name %s", tid_app,
                         app_name.c_str());
    }(tid, app_name));
}

void ClientManager::replyToClient(const pid_t tid, const off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, offset=%ld)", tid, offset);
    std::shared_ptr<ClientState> state;
    {
        const std::lock_guard lock(clients_mutex);
        state = clients.at(tid);
    }
    if (!tryReply({tid, state->generation, state}, offset)) {
        throw std::runtime_error("client is no longer active");
    }
}

bool ClientManager::tryReply(const ClientToken &client, const off64_t offset) {
    std::shared_ptr<CircularBuffer<off64_t>> response;
    {
        const std::lock_guard lock(client.state->lifecycle_mutex);
        if (shutting_down || !client.state->active ||
            client.state->generation != client.generation) {
            return false;
        }
        ++client.state->replies_in_flight;
        response = client.state->response;
    }

    try {
        response->write(&offset);
    } catch (...) {
        const std::lock_guard lock(client.state->lifecycle_mutex);
        if (--client.state->replies_in_flight == 0) {
            client.state->lifecycle_cv.notify_all();
        }
        throw;
    }
    {
        const std::lock_guard lock(client.state->lifecycle_mutex);
        if (--client.state->replies_in_flight == 0) {
            client.state->lifecycle_cv.notify_all();
        }
    }
    return true;
}

bool ClientManager::isClientActive(const ClientToken &client) const {
    return !shutting_down && client.state->active &&
           client.state->generation == client.generation;
}

bool ClientManager::spawnWaiter(const pid_t tid,
                                std::function<void(const ClientToken &)> waiter) {
    reapCompletedWaiters();
    ClientToken client;
    {
        const std::lock_guard lock(clients_mutex);
        if (shutting_down) {
            return false;
        }
        const auto state = clients.find(tid);
        if (state == clients.end()) {
            return false;
        }
        client = {tid, state->second->generation, state->second};
    }
    auto completed = std::make_shared<std::atomic<bool>>(false);
    {
        const std::lock_guard lock(waiter_threads_mutex);
        if (shutting_down) {
            return false;
        }
        waiter_threads.reserve(waiter_threads.size() + 1);

        sigset_t signals, previous_signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGTERM);
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGQUIT);
        sigaddset(&signals, SIGPIPE);
        sigaddset(&signals, SIGABRT);
        sigaddset(&signals, SIGILL);
        sigaddset(&signals, SIGFPE);
        sigaddset(&signals, SIGSEGV);
        int error = pthread_sigmask(SIG_BLOCK, &signals, &previous_signals);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_sigmask block");
        }
        try {
            std::thread thread([client, completed, waiter = std::move(waiter)] {
                struct MarkCompleted {
                    std::shared_ptr<std::atomic<bool>> completed;
                    ~MarkCompleted() { completed->store(true); }
                } mark_completed{completed};
                waiter(client);
            });
            waiter_threads.push_back({std::move(thread), completed});
        } catch (...) {
            error = pthread_sigmask(SIG_SETMASK, &previous_signals, nullptr);
            if (error != 0) {
                std::terminate();
            }
            throw;
        }
        error = pthread_sigmask(SIG_SETMASK, &previous_signals, nullptr);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_sigmask restore");
        }
    }
    return true;
}

void ClientManager::replyToClient(const int tid, const off64_t offset, char *buf,
                                  const off64_t count) {
    START_LOG(gettid(), "call(tid=%d, buf=0x%08x, offset=%ld, count=%ld)", tid, buf, offset, count);

    if (const auto out = data_buffers.find(tid); out != data_buffers.end()) {
        this->replyToClient(tid, offset + count);
        out->second.ServerToClient.write(buf + offset, count);
        return;
    }
    throw std::runtime_error("Err: no such buffer for provided tid");
}

// NOTE: do not use const reference for path here as the emplace method leaves the original in an
// invalid state
void ClientManager::registerProducedFile(const pid_t tid, std::string path) {
    START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path.c_str());
    if (const auto itm = files_created_by_producer.find(tid);
        itm != files_created_by_producer.end()) {
        itm->second.emplace_back(path);
    } else {
        LOG("Error: tid is not present in files_created_by_producers map");
        return;
    }
    const std::string &app_name = this->getAppName(tid);
    if (const auto itm = files_created_by_app_name.find(app_name);
        itm != files_created_by_app_name.end()) {
        itm->second.emplace_back(path);

    } else {
        LOG("Error: app_name is not present in files_created_by_app_name map");
    }
}

void ClientManager::removeProducedFile(const pid_t tid, const std::filesystem::path &path) {
    if (const auto itm = files_created_by_producer.find(tid);
        itm != files_created_by_producer.end()) {
        auto &v = itm->second;
        v.erase(std::remove(v.begin(), v.end(), path), v.end());
    }

    const std::string &app_name = this->getAppName(tid);
    if (const auto itm = files_created_by_app_name.find(app_name);
        itm != files_created_by_app_name.end()) {
        auto &v = itm->second;
        v.erase(std::remove(v.begin(), v.end(), path), v.end());
    }
}

bool ClientManager::isProducer(const pid_t tid, const std::filesystem::path &path) const {
    bool is_producer = false;

    if (const auto itm = files_created_by_producer.find(tid);
        itm != files_created_by_producer.end()) {
        is_producer |= std::find(itm->second.begin(), itm->second.end(), path) != itm->second.end();
    }

    const std::string &app_name = this->getAppName(tid);
    if (const auto itm = files_created_by_app_name.find(app_name);
        itm != files_created_by_app_name.end()) {
        is_producer |= std::find(itm->second.begin(), itm->second.end(), path) != itm->second.end();
    }

    return is_producer;
}

const std::vector<std::string> &ClientManager::getProducedFiles(const pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    return files_created_by_producer[tid];
}

std::string ClientManager::getAppName(const pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    const std::lock_guard lock(clients_mutex);
    if (const auto itm = app_names.find(tid); itm != app_names.end()) {
        return itm->second;
    } else {
        return default_app_name;
    }
}

SPSCQueue &ClientManager::getClientToServerDataBuffers(const pid_t tid) {
    return data_buffers.at(tid).ClientToServer;
}

size_t ClientManager::getConnectedPosixClients() const { return data_buffers.size(); }

int ClientManager::readNextRequest(char *str) {
    char req[CAPIO_REQ_MAX_SIZE];
    requests.read(req);
    START_LOG(gettid(), "call(req=%s)", req);
    int code       = -1;
    auto [ptr, ec] = std::from_chars(req, req + 4, code);
    if (ec == std::errc()) {
        strcpy(str, ptr + 1);
    } else {

        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Received invalid request: %s", str);
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Code: %d is not mapped to a valid request handler",
                         code);

        return -1;
    }
    return code;
}
