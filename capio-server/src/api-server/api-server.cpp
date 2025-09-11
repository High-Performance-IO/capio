#include "capio/constants.hpp"
#include "include/client-manager/client_manager.hpp"
#include "include/utils/configuration.hpp"

#include <include/api-server/api-server.hpp>

void CapioAPIServer::api_server_main_func(int server_port, httplib::Server *svr) {

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "Started API server on port: " + std::to_string(server_port));

    svr->Get("/", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("{\"hostname\" : \"" +
                            std::string(capio_global_configuration->node_name) +
                            "\",\"wf_name\" : \"" + capio_global_configuration->workflow_name +
                            "\"}",
                        "application/json");
    });

    svr->Get("/clients", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("{\"connected_clients\":\"" +
                            std::to_string(client_manager->get_connected_posix_client()) + "\"}",
                        "application/json");
    });

    svr->Get("/terminate", [](const httplib::Request &req, httplib::Response &res) {
        server_println(CAPIO_SERVER_CLI_LOG_SERVER_WARNING,
                       "Received shutdown request from API Server");
        capio_global_configuration->termination_phase = true;
        res.set_content("{\"status\":\"shutdown\"}", "application/json");
        kill(capio_global_configuration->CAPIO_SERVER_MAIN_PID,
             SIGUSR1); // Wake parent child and children
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
