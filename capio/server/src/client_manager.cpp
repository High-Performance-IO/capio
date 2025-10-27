#include <thread>

#include "utils/capiocl_adapter.hpp"

extern std::string workflow_name;

#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "common/queue.hpp"
#include "utils/common.hpp"

ClientManager::ClientManager() {
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
    files_created_by_producer[tid];
    files_created_by_app_name[app_name];
    register_listener(tid);

    if (wait) {
        std::thread t([&, target_tid = tid]() {
            std::unique_lock<std::mutex> lock(mutex_thread_allowed_to_continue);
            cv_thread_allowed_to_continue.wait(lock, [&, target_tid]() {
                return std::find(thread_allowed_to_continue.begin(),
                                 thread_allowed_to_continue.end(),
                                 target_tid) != thread_allowed_to_continue.end();
            });
            write_response(target_tid, 1);
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
    remove_listener(tid);
}

void ClientManager::replyToClient(const int tid, char *buf, const off64_t offset,
                                  const off64_t count) const {
    START_LOG(gettid(), "call(tid=%d, buf=0x%08x, offset=%ld, count=%ld)", tid, buf, offset, count);

    if (const auto out = data_buffers.find(tid); out != data_buffers.end()) {
        write_response(tid, offset + count);
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
    if (const auto itm = files_created_by_producer.find(tid);
        itm == files_created_by_producer.end()) {
        files_created_by_producer[tid];
    }
    return files_created_by_producer.at(tid);
}

const std::string &ClientManager::getAppName(const pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    static std::string default_app_name = CAPIO_DEFAULT_APP_NAME;
    if (const auto itm = app_names.find(tid); itm != app_names.end()) {
        return itm->second;
    }
    return default_app_name;
}

SPSCQueue &ClientManager::getClientToServerDataBuffers(const pid_t tid) const {
    return *data_buffers.at(tid).ClientToServer;
}

const size_t ClientManager::getConnectedPosixClients() const { return data_buffers.size(); }