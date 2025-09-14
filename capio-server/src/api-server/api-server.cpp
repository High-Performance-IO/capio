#include <capio/constants.hpp>
#include <include/api-server/api-server.hpp>
#include <include/client-manager/client_manager.hpp>
#include <include/utils/configuration.hpp>

std::string
CapioAPIServer::build_json_response(std::unordered_map<std::string, std::string> &response_map) {

    response_map["hostname"] = capio_global_configuration->node_name;
    response_map["wf_name"]  = capio_global_configuration->workflow_name;

    std::string json_response = "{";

    for (auto &[key, value] : response_map) {
        json_response += "\"" + key + "\":\"";
        json_response += value + "\",";
    }

    // Remove last comma to ensure json validity
    if (json_response.back() == ',') {
        json_response.pop_back();
    }

    json_response += "}";
    return json_response;
}

void CapioAPIServer::api_server_main_func(int server_port, httplib::Server *svr) {

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "Started API server on port: " + std::to_string(server_port));

    svr->Get("/", [](const httplib::Request &req, httplib::Response &res) {
        ResponseMap map;
        res.set_content(build_json_response(map).c_str(), "application/json");
    });

    svr->Get("/clients", [](const httplib::Request &req, httplib::Response &res) {
        ResponseMap map;
        map["connected_clients"] = std::to_string(client_manager->get_connected_posix_client());

        res.set_content(build_json_response(map).c_str(), "application/json");
    });

    svr->Get("/terminate", [](const httplib::Request &req, httplib::Response &res) {
        server_println(CAPIO_SERVER_CLI_LOG_SERVER_WARNING,
                       "Received shutdown request from API Server");
        capio_global_configuration->termination_phase = true;
        ResponseMap map;
        map["status"] = "shutting-down";
        res.set_content(build_json_response(map).c_str(), "application/json");
        kill(capio_global_configuration->CAPIO_SERVER_MAIN_PID,
             SIGUSR1); // Wake parent child and children
    });

    svr->set_error_handler([](const httplib::Request &req, httplib::Response &res) {
        ResponseMap map;

        map["status"]  = std::to_string(res.status);
        map["message"] = "Error: Unknown request: " + req.path;
        res.set_content(build_json_response(map).c_str(), "application/json");
    });

    svr->listen("*", server_port);
}

CapioAPIServer::CapioAPIServer(int server_port) {
    th = new std::thread(api_server_main_func, server_port, &svr);
}

CapioAPIServer::~CapioAPIServer() {
    svr.stop();
    th->join();
    delete th;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "API server correctly terminated");
}
