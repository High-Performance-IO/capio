#include "remote/backend.hpp"

Backend::Backend(const unsigned int node_name_max_length)
    : n_servers(1), node_name(node_name_max_length, '\0') {
    gethostname(node_name.data(), node_name_max_length);
}

[[nodiscard]] const std::string &Backend::get_node_name() const {
    START_LOG(gettid(), "call()");
    LOG("THIS node_name = %s", node_name.c_str());
    return node_name;
}

std::set<std::string> Backend::get_nodes() { return {node_name}; }