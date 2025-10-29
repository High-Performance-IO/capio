#include "utils/capiocl_adapter.hpp"

#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "common/queue.hpp"
#include "utils/common.hpp"

ClientManager::ClientManager() {
    START_LOG(gettid(), "call()");
    data_buffers              = new std::unordered_map<long, std::pair<SPSCQueue *, SPSCQueue *>>();
    app_names                 = new std::unordered_map<int, const std::string>;
    files_created_by_producer = new std::unordered_map<pid_t, std::vector<std::string> *>;
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "ClientManager initialization completed.");
}

ClientManager::~ClientManager() {
    START_LOG(gettid(), "call()");
    delete data_buffers;
    delete app_names;
    delete files_created_by_producer;
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "ClientManager cleanup completed.");
}

void ClientManager::register_client(const std::string &app_name, pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld, app_name=%s)", tid, app_name.c_str());
    // TODO: replace numbers with constexpr
    const auto workflow_name = get_capio_workflow_name();
    data_buffers->insert(
        {tid,
         {new SPSCQueue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), get_cache_lines(),
                        get_cache_line_size(), workflow_name),
          new SPSCQueue(SHM_SPSC_PREFIX_READ + std::to_string(tid), get_cache_lines(),
                        get_cache_line_size(), workflow_name)}});
    app_names->emplace(tid, app_name);
    files_created_by_producer->emplace(tid, new std::vector<std::string>);
}

void ClientManager::remove_client(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    if (const auto it_resp = data_buffers->find(tid); it_resp != data_buffers->end()) {
        delete it_resp->second.first;
        delete it_resp->second.second;
        data_buffers->erase(it_resp);
    }
    files_created_by_producer->erase(tid);
}

void ClientManager::reply_to_client(const pid_t tid, char *response, size_t response_size) const {
    START_LOG(gettid(), "call(tid=%ld, offset=%s)", tid, response);
    if (const auto out = data_buffers->find(tid); out != data_buffers->end()) {
        out->second.second->write(response, response_size);
        return;
    }
    LOG("Err: no such buffer for provided tid");
}

void ClientManager::register_produced_file(pid_t tid, std::string path) const {
    START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path.c_str());
    if (const auto itm = files_created_by_producer->find(tid);
        itm != files_created_by_producer->end()) {
        itm->second->emplace_back(path);
        return;
    }
    LOG("Error: tis is not present in files_created_by_producers map");
}

std::vector<std::string> *ClientManager::get_produced_files(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    if (const auto itm = files_created_by_producer->find(tid);
        itm == files_created_by_producer->end()) {
        files_created_by_producer->emplace(tid, new std::vector<std::string>());
    }
    return files_created_by_producer->at(tid);
}

std::string ClientManager::get_app_name(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    if (const auto itm = app_names->find(tid); itm != app_names->end()) {
        return itm->second;
    }
    return CAPIO_DEFAULT_APP_NAME;
}

size_t ClientManager::get_connected_posix_client() { return data_buffers->size(); }