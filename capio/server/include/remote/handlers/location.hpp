#ifndef CAPIO_SERVER_REMOTE_HANDLERS_LOCATION_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_LOCATION_HPP

#include <cstdio>

#include "calf/StlLogger.h"
#include "remote/backend.hpp"
#include "remote/home_node_service.hpp"

// Requests of the HomeNodeService. The path is always the last field and the rest of the
// request, so that it may hold spaces.

/// Another server created the file, and is its home: "<path>"
inline void remote_location_announce_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, path=%s)", request.get_source().c_str(),
              request.get_content());
    home_node_service->learn(request.get_content(), request.get_source());
}

/// Another server removed a file it was home of: "<path>"
inline void remote_location_forget_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, path=%s)", request.get_source().c_str(),
              request.get_content());
    home_node_service->forget(request.get_content(), request.get_source());
}

/// Another server asks whether this one holds a file: "<query id> <path>"
inline void remote_location_query_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, content=%s)", request.get_source().c_str(),
              request.get_content());
    long query_id;
    int path_start = 0;
    if (sscanf(request.get_content(), "%ld %n", &query_id, &path_start) < 1 || path_start == 0) {
        LOG("malformed location query");
        return;
    }
    home_node_service->answerQuery(query_id, request.get_content() + path_start,
                                   request.get_source());
}

/// Another server answers a query of this one: "<query id> <1 if it holds the file, 0 if not>
/// <path>"
inline void remote_location_answer_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, content=%s)", request.get_source().c_str(),
              request.get_content());
    long query_id;
    int holds;
    if (sscanf(request.get_content(), "%ld %d", &query_id, &holds) < 2) {
        LOG("malformed location answer");
        return;
    }
    home_node_service->receiveAnswer(query_id, holds != 0, request.get_source());
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_LOCATION_HPP
