#ifndef CAPIO_SERVER_REMOTE_HANDLERS_DIRECTORY_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_DIRECTORY_HPP

#include "calf/StlLogger.h"
#include "remote/backend.hpp"
#include "storage/manager.hpp"

extern StorageManager *storage_manager;

// Requests to the home of a directory, from a server that holds a stand-in for it. The path is
// the whole content, so that it may hold spaces.

/// Another server created a file in a directory this one is home of: "<path of the file>"
inline void remote_directory_entry_add_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, path=%s)", request.get_source().c_str(),
              request.get_content());
    storage_manager->addRemoteDirectoryEntry(request.get_content());
}

/// Another server removed a file from a directory this one is home of: "<path of the file>"
inline void remote_directory_entry_remove_handler(const RemoteRequest &request) {
    START_LOG(gettid(), "call(source=%s, path=%s)", request.get_source().c_str(),
              request.get_content());
    storage_manager->removeFromDirectory(request.get_content());
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_DIRECTORY_HPP
