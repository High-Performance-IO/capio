#ifndef CAPIO_API_SERVER_HPP
#define CAPIO_API_SERVER_HPP
#include <httplib.h>
#include <include/utils/configuration.hpp>
#include <string>
#include <thread>
#include <unordered_map>

inline std::unordered_map<std::string, std::string> api_server_routes_descriptions;

#define REGISTER_GET_ROUTE(route_name, route_description, callback)                                \
    api_server_routes_descriptions[route_name] = route_description;                                \
    svr->Get(route_name, callback);

class CapioAPIServer {
    typedef std::unordered_map<std::string, std::string> ResponseMap;

    std::thread *th;
    httplib::Server httplib_server_instance;
    static void api_server_main_func(int server_port, httplib::Server *svr);

    static std::string
    build_json_response(const std::unordered_map<std::string, std::string> &map) {

        // TODO: implement a correct json mapping in c++

        ResponseMap response_map = map;
        response_map["identity"] = std::string("{ \"hostname\":\"") +
                                   capio_global_configuration->node_name + "\",\"wf_name\":\"" +
                                   capio_global_configuration->workflow_name + "\"},";

        std::string json_response = "{";

        for (auto &[key, value] : response_map) {
            if (key == "identity") {
                json_response += "\"" + key + "\":" + value;
            } else {
                json_response += "\"" + key + "\":\"";
                json_response += value + "\",";
            }
        }

        // Remove last comma to ensure json validity
        if (json_response.back() == ',') {
            json_response.pop_back();
        }

        json_response += "}\n";
        return json_response;
    }

  public:
    explicit CapioAPIServer(int server_port);
    ~CapioAPIServer();
};

inline CapioAPIServer *api_server;

#endif // CAPIO_API_SERVER_HPP
