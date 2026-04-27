#include "remote/backend.hpp"

#include "utils/common.hpp"

#include <iostream>

Backend::Backend(const unsigned int node_name_max_length)
    : n_servers(1), node_name(node_name_max_length, '\0') {
    // Note: default instantiation of node_name. the content of node_name may be changed by specific
    // derived backend classes.

    gethostname(node_name.data(), node_name_max_length);
    node_name.resize(strlen(node_name.data()));
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Backend", "Node name: " + node_name);
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Backend",
                   "Node count: " + std::to_string(n_servers));
}

[[nodiscard]] const std::string &Backend::get_node_name() const {
    START_LOG(gettid(), "call()");
    LOG("THIS node_name = %s", node_name.c_str());
    return node_name;
}

const std::set<std::string> Backend::get_nodes() { return {node_name}; }