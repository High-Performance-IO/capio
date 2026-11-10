#include <cstdio>
#include <unistd.h>
#include <vector>

#include "calf/StlLogger.h"
#include "common/requests.hpp"
#include "remote/home_node_service.hpp"

namespace {

/// @brief The other servers this one knows of
std::vector<std::string> other_servers() {
    std::vector<std::string> servers;
    for (const auto &node : backend->get_nodes()) {
        if (node != backend->get_node_name()) {
            servers.push_back(node);
        }
    }
    return servers;
}

/// @brief A request to another server: the code, the fields, and the path last, so that the path
/// is the rest of the request and may hold spaces
std::string location_request(const int code, const std::string &fields,
                             const std::filesystem::path &path) {
    char code_str[16];
    snprintf(code_str, sizeof(code_str), "%04d ", code);
    std::string message = code_str + fields + path.native();
    // The backends send message_len bytes (MTCL) or message_len + 1 (MPI): end the message with
    // a terminator of its own, so that both read only what the string holds.
    message.push_back('\0');
    return message;
}

} // namespace

HomeNodeService::HomeNodeService(const std::chrono::milliseconds query_timeout,
                                 const std::chrono::milliseconds requery_interval)
    : _query_timeout(query_timeout), _requery_interval(requery_interval) {}

void HomeNodeService::_send(const int code, const std::string &fields,
                            const std::filesystem::path &path, const std::string &target) {
    START_LOG(gettid(), "call(code=%d, fields=%s, path=%s, target=%s)", code, fields.c_str(),
              path.c_str(), target.c_str());
    const std::string message = location_request(code, fields, path);
    backend->send_request(message.c_str(), static_cast<int>(message.size()), target);
}

std::size_t HomeNodeService::_broadcast(const int code, const std::string &fields,
                                        const std::filesystem::path &path) {
    START_LOG(gettid(), "call(code=%d, fields=%s, path=%s)", code, fields.c_str(), path.c_str());
    const auto servers = other_servers();
    for (const auto &server : servers) {
        _send(code, fields, path, server);
    }
    return servers.size();
}

std::optional<std::string> HomeNodeService::getHome(const std::filesystem::path &path) const {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    const std::lock_guard lg(_mutex);
    if (const auto it = _homes.find(path); it != _homes.end()) {
        LOG("path %s is on node %s", path.c_str(), it->second.c_str());
        return it->second;
    }
    LOG("path %s is not known to this server", path.c_str());
    return {};
}

std::optional<std::string> HomeNodeService::lookup(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    const auto servers = other_servers();
    long query_id;
    {
        const std::lock_guard lg(_mutex);
        if (const auto it = _homes.find(path); it != _homes.end()) {
            return it->second;
        }
        if (servers.empty()) {
            return {};
        }
        // Registered before any request is sent, so that no answer arrives before its query
        query_id = _next_query_id++;
        _queries.emplace(query_id, Query{servers.size(), {}});
    }

    const std::string fields = std::to_string(query_id) + " ";
    for (const auto &server : servers) {
        _send(CAPIO_SERVER_REQUEST_LOCATION_QUERY, fields, path, server);
    }

    std::unique_lock lock(_mutex);
    const bool answered = _changed.wait_for(lock, _query_timeout, [this, query_id, &path] {
        const auto &query = _queries.at(query_id);
        return query.home || query.awaited == 0 || _homes.find(path) != _homes.end();
    });
    const Query query   = _queries.at(query_id);
    _queries.erase(query_id);
    if (!answered) {
        LOG("lookup of %s timed out with %zu answers missing", path.c_str(), query.awaited);
    }

    if (const auto it = _homes.find(path); it != _homes.end()) {
        return it->second; // announced while this server was asking
    }
    if (query.home) {
        _homes.emplace(path, *query.home);
        return query.home;
    }
    LOG("no running server holds %s", path.c_str());
    return {};
}

std::string HomeNodeService::waitForHome(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    while (true) {
        if (auto home = lookup(path)) {
            return *home;
        }
        // The announcement of its creation wakes this wait; asking again now and then covers a
        // server this one could not hear from yet, such as one that connected later.
        std::unique_lock lock(_mutex);
        if (_changed.wait_for(lock, _requery_interval,
                              [this, &path] { return _homes.find(path) != _homes.end(); })) {
            return _homes.at(path);
        }
    }
}

void HomeNodeService::setHome(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    {
        const std::lock_guard lg(_mutex);
        _homes[path] = backend->get_node_name();
    }
    _changed.notify_all();
    _broadcast(CAPIO_SERVER_REQUEST_LOCATION_ANNOUNCE, "", path);
}

int HomeNodeService::remove(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    bool was_home;
    {
        const std::lock_guard lg(_mutex);
        const auto it = _homes.find(path);
        if (it == _homes.end()) {
            LOG("path %s is not known to this server", path.c_str());
            return 2;
        }
        was_home = it->second == backend->get_node_name();
        _homes.erase(it);
    }
    if (was_home) {
        _broadcast(CAPIO_SERVER_REQUEST_LOCATION_FORGET, "", path);
    }
    return 1;
}

void HomeNodeService::rename(const std::filesystem::path &oldpath,
                             const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());
    remove(oldpath);
    if (!getHome(newpath)) {
        setHome(newpath);
    }
}

void HomeNodeService::learn(const std::filesystem::path &path, const std::string &home) {
    START_LOG(gettid(), "call(path=%s, home=%s)", path.c_str(), home.c_str());
    {
        const std::lock_guard lg(_mutex);
        const auto [it, inserted] = _homes.emplace(path, home);
        if (!inserted && it->second != backend->get_node_name()) {
            it->second = home;
        }
    }
    _changed.notify_all();
}

void HomeNodeService::forget(const std::filesystem::path &path, const std::string &home) {
    START_LOG(gettid(), "call(path=%s, home=%s)", path.c_str(), home.c_str());
    const std::lock_guard lg(_mutex);
    if (const auto it = _homes.find(path); it != _homes.end() && it->second == home) {
        _homes.erase(it);
    }
}

void HomeNodeService::answerQuery(const long query_id, const std::filesystem::path &path,
                                  const std::string &source) const {
    START_LOG(gettid(), "call(query_id=%ld, path=%s, source=%s)", query_id, path.c_str(),
              source.c_str());
    bool holds;
    {
        const std::lock_guard lg(_mutex);
        const auto it = _homes.find(path);
        holds         = it != _homes.end() && it->second == backend->get_node_name();
    }
    _send(CAPIO_SERVER_REQUEST_LOCATION_ANSWER,
          std::to_string(query_id) + " " + (holds ? "1" : "0") + " ", path, source);
}

void HomeNodeService::receiveAnswer(const long query_id, const bool holds,
                                    const std::string &source) {
    START_LOG(gettid(), "call(query_id=%ld, holds=%s, source=%s)", query_id,
              holds ? "true" : "false", source.c_str());
    {
        const std::lock_guard lg(_mutex);
        const auto it = _queries.find(query_id);
        if (it == _queries.end()) {
            LOG("query %ld already ended", query_id);
            return;
        }
        if (holds) {
            it->second.home = source;
        }
        if (it->second.awaited > 0) {
            it->second.awaited--;
        }
    }
    _changed.notify_all();
}
