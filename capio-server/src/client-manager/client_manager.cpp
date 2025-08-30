#include <include/client-manager/client_manager.hpp>

ClientManager::ClientManager() {
    START_LOG(gettid(), "call()");
    bufs_response             = new std::unordered_map<long, ResponseQueue *>();
    app_names                 = new std::unordered_map<int, const std::string>;
    files_created_by_producer = new std::unordered_map<pid_t, std::vector<std::string> *>;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "ClientManager initialization completed.");
}

ClientManager::~ClientManager() {
    START_LOG(gettid(), "call()");
    delete bufs_response;
    delete app_names;
    delete files_created_by_producer;
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "ClientManager cleanup completed.");
}

void ClientManager::register_client(const std::string &app_name, pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld, app_name=%s)", tid, app_name.c_str());
    // TODO: replace numbers with constexpr
    auto *p_buf_response = new ResponseQueue(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), false);

    bufs_response->insert(std::make_pair(tid, p_buf_response));
    app_names->emplace(tid, app_name);
    files_created_by_producer->emplace(tid, new std::vector<std::string>);
}

void ClientManager::remove_client(pid_t tid) const {
    START_LOG(gettid(), "call(tid=%ld)", tid);
    if (const auto it_resp = bufs_response->find(tid); it_resp != bufs_response->end()) {
        delete it_resp->second;
        bufs_response->erase(it_resp);
    }
    files_created_by_producer->erase(tid);
}

void ClientManager::reply_to_client(const pid_t tid, const capio_off64_t offset) const {
    START_LOG(gettid(), "call(tid=%ld, offset=%llu)", tid, offset);
    if (const auto out = bufs_response->find(tid); out != bufs_response->end()) {
        out->second->write(offset);
        return;
    }
    LOG("Err: no such buffer for provided tid");
}

void ClientManager::register_produced_file(pid_t tid, std::string &path) const {
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

size_t ClientManager::get_connected_posix_client() { return bufs_response->size(); }