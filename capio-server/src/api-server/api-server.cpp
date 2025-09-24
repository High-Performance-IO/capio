#include <capio/constants.hpp>
#include <include/api-server/api-server.hpp>
#include <include/client-manager/client_manager.hpp>

CapioAPIServer::CapioAPIServer(int server_port) {
    th = new std::thread(api_server_main_func, server_port, &httplib_server_instance);

    // Register callback for unknown routes
    httplib_server_instance.set_error_handler(
        [](const httplib::Request &req, httplib::Response &res) {
            ResponseMap map;
            map["status"]  = std::to_string(res.status);
            map["message"] = "Error: Unknown request: " + req.path;
            res.set_content(build_json_response(map).c_str(), "application/json");
        });
}

CapioAPIServer::~CapioAPIServer() {
    httplib_server_instance.stop();
    th->join();
    delete th;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "API server correctly terminated");
}

void CapioAPIServer::api_server_main_func(const int server_port, httplib::Server *svr) {

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "Started API server on port: " + std::to_string(server_port));

    REGISTER_GET_ROUTE("/", "Get server instance information",
                       [](const httplib::Request &req, httplib::Response &res) {
                           ResponseMap map;
                           map["endpoints"] = "/routes";
                           res.set_content(build_json_response(map).c_str(), "application/json");
                       });

    REGISTER_GET_ROUTE("/clients", "Get number connected POSIX clients",
                       [](const httplib::Request &req, httplib::Response &res) {
                           ResponseMap map;
                           map["connected_clients"] =
                               std::to_string(client_manager->get_connected_posix_client());

                           res.set_content(build_json_response(map).c_str(), "application/json");
                       });

    REGISTER_GET_ROUTE("/terminate", "Terminate gracefully server instance",
                       [](const httplib::Request &req, httplib::Response &res) {
                           server_println(CAPIO_SERVER_CLI_LOG_SERVER_WARNING,
                                          "Received shutdown request from API Server");
                           capio_global_configuration->termination_phase = true;
                           ResponseMap map;
                           map["status"] = "shutting-down";
                           res.set_content(build_json_response(map).c_str(), "application/json");
                           kill(capio_global_configuration->CAPIO_SERVER_MAIN_PID,
                                SIGUSR1); // Wake parent child and children
                       });

    REGISTER_GET_ROUTE("/routes", "Get all available API-SERVER routes",
                       [](const httplib::Request &req, httplib::Response &res) {
                           res.set_content(
                               build_json_response(api_server_routes_descriptions).c_str(),
                               "application/json");
                       });

    REGISTER_GET_ROUTE("/status", "Get current server status",
                       [](const httplib::Request &req, httplib::Response &res) {
                           ResponseMap map;
                           map["status"] = capio_global_configuration->termination_phase
                                               ? "shutting-down"
                                               : "running";
                           res.set_content(build_json_response(map).c_str(), "application/json");
                       });

    svr->listen("127.0.0.1", server_port);
    server_println(CAPIO_SERVER_CLI_LOG_SERVER_ERROR, "API server terminated unexpectedly");
}
