#include <thread>

#include "utils/capiocl_adapter.hpp"

extern std::string workflow_name;

#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "common/queue.hpp"
#include "utils/common.hpp"

ClientManager::ClientManager()
    : requests(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name) {
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "ClientManager initialization completed.");
}

ClientManager::~ClientManager() {
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "ClientManager teardown completed.");
}

void ClientManager::registerClient(pid_t tid, const std::string &app_name, const bool wait) {
    START_LOG(gettid(), "call(tid=%ld, app_name=%s)", tid, app_name.c_str());

    ClientDataBuffers buffers{
        new SPSCQueue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), get_cache_lines(),
                      get_cache_line_size(), workflow_name),
        new SPSCQueue(SHM_SPSC_PREFIX_READ + std::to_string(tid), get_cache_lines(),
                      get_cache_line_size(), workflow_name)};

    data_buffers.emplace(tid, buffers);
    app_names.emplace(tid, app_name);
    files_created_by_producer.emplace(tid, std::initializer_list<std::string>());
    files_created_by_app_name.emplace(app_name, std::initializer_list<std::string>());

    responses.try_emplace(tid, SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT,
                          sizeof(off_t), workflow_name);

    if (wait) {
        std::thread t([&, target_tid = tid]() {
            std::unique_lock<std::mutex> lock(mutex_thread_allowed_to_continue);
            cv_thread_allowed_to_continue.wait(lock, [&, target_tid]() {
                return std::find(thread_allowed_to_continue.begin(),
                                 thread_allowed_to_continue.end(),
                                 target_tid) != thread_allowed_to_continue.end();
            });
            this->replyToClient(target_tid, 1);
            const auto it = std::find(thread_allowed_to_continue.begin(),
                                      thread_allowed_to_continue.end(), target_tid);
            thread_allowed_to_continue.erase(it);
        });
        t.detach();
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
    if (const auto it_resp = data_buffers.find(tid); it_resp != data_buffers.end()) {
        delete it_resp->second.ClientToServer;
        delete it_resp->second.ServerToClient;
        data_buffers.erase(it_resp);
    }
    const std::string &app_name = this->getAppName(tid);
    files_created_by_producer.erase(tid);
    files_created_by_app_name.erase(app_name);

    if (const auto response_buffer = responses.find(tid); response_buffer != responses.end()) {
        responses.erase(response_buffer);
    }
}

void ClientManager::replyToClient(const pid_t tid, const off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, offset=%ld)", tid, offset);
    responses.at(tid).write(&offset);
}

void ClientManager::replyToClient(const int tid, const off64_t offset, char *buf,
                                  const off64_t count) {
    START_LOG(gettid(), "call(tid=%d, buf=0x%08x, offset=%ld, count=%ld)", tid, buf, offset, count);

    if (const auto out = data_buffers.find(tid); out != data_buffers.end()) {
        this->replyToClient(tid, offset + count);
        out->second.ServerToClient->write(buf + offset, count);
        return;
    }
    LOG("Err: no such buffer for provided tid");
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

const std::string &ClientManager::getAppName(const pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    static std::string default_app_name = CAPIO_DEFAULT_APP_NAME;
    if (const auto itm = app_names.find(tid); itm != app_names.end()) {
        return itm->second;
    }
    return default_app_name;
}

SPSCQueue &ClientManager::getClientToServerDataBuffers(const pid_t tid) {
    return *data_buffers.at(tid).ClientToServer;
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
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                  << std::endl;
        ERR_EXIT("Invalid request %d%s", code, ptr);
    }
    return code;
}