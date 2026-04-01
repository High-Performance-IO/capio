#include "remote/backend.hpp"

#include <iostream>

Backend::Backend(const unsigned int node_name_max_length)
    : n_servers(1), node_name(node_name_max_length, '\0') {
    // Note: default instantiation of node_name. the content of node_name may be changed by specific
    // derived backend classes.

    gethostname(node_name.data(), node_name_max_length);
    node_name.resize(strlen(node_name.data()));

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " Backend] Node name: " << node_name
              << std::endl;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " Backend] Node Count: " << n_servers
              << std::endl;
}

[[nodiscard]] const std::string &Backend::get_node_name() const {
    START_LOG(gettid(), "call()");
    LOG("THIS node_name = %s", node_name.c_str());
    return node_name;
}

const std::set<std::string> Backend::get_nodes() { return {node_name}; }