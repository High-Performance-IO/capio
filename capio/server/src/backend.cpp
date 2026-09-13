#include "remote/backend.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "utils/common.hpp"

#include <iostream>

Backend::Backend(const unsigned int node_name_max_length)
    : n_servers(1), node_name(node_name_max_length, '\0') {
    // Note: default instantiation of node_name. the content of node_name may be changed by specific
    // derived backend classes.

    gethostname(node_name.data(), node_name_max_length);
    node_name.resize(strlen(node_name.data()));
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Node name: %s", node_name.c_str());
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Node count: %d", n_servers);
}

[[nodiscard]] const std::string &Backend::get_node_name() const {
    START_LOG(gettid(), "call()");
    LOG("THIS node_name = %s", node_name.c_str());
    return node_name;
}

const std::set<std::string> Backend::get_nodes() { return {node_name}; }