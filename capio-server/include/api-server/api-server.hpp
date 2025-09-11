#ifndef CAPIO_API_SERVER_HPP
#define CAPIO_API_SERVER_HPP
#include <httplib.h>
#include <thread>
class CapioAPIServer {
    std::thread *th;
    httplib::Server svr;
    static void api_server_main_func(int server_port, httplib::Server *svr);

  public:
    CapioAPIServer(int server_port);
    ~CapioAPIServer();
};

inline CapioAPIServer *api_server;

#endif // CAPIO_API_SERVER_HPP
