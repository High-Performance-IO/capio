#ifndef CAPIO_SERVER_UTILS_SERVER_STATE_HPP
#define CAPIO_SERVER_UTILS_SERVER_STATE_HPP

/*
 * The state of a server, declared once. capio_server.cpp defines it, and so does the main of the
 * server unit tests. The classes are only declared here: include the header of the one you use.
 * The CAPIO-CL engine is not here: the server reads it through CapioCLEngine::get(), and only the
 * code that has to change it declares the writable pointer.
 */

class Backend;
class ClientManager;
class DiscoveryService;
class FsStorageManager;
class HomeNodeService;
class StorageManager;

extern Backend *backend;                     ///< the connection to the other servers
extern ClientManager *client_manager;        ///< the clients of this server, and their queues
extern DiscoveryService *discovery_service;  ///< how this server finds the others
extern FsStorageManager *fs_storage_manager; ///< the files of the FS variant
extern HomeNodeService *home_node_service;   ///< which server is the home of each file
extern StorageManager *storage_manager;      ///< the files of the memory variant

#endif // CAPIO_SERVER_UTILS_SERVER_STATE_HPP
