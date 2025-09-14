#ifndef CAPIO_API_SERVER_HPP
#define CAPIO_API_SERVER_HPP
#include <httplib.h>
#include <string>
#include <thread>
#include <unordered_map>

class CapioAPIServer {
    typedef std::unordered_map<std::string, std::string> ResponseMap;

    std::thread *th;
    httplib::Server svr;
    static void api_server_main_func(int server_port, httplib::Server *svr);
    static std::string
    build_json_response(std::unordered_map<std::string, std::string> &response_map);

  public:
    explicit CapioAPIServer(int server_port);
    ~CapioAPIServer();
};

inline CapioAPIServer *api_server;

#endif // CAPIO_API_SERVER_HPP
