#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include "capio/logger.hpp" //se lo tongo non vann i log
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <list>
#include <mtcl.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class CapioCommunicationService : BackendInterface {

  private:
    std::unordered_map<std::string, MTCL::HandleUser> ConnectedHostnames;
    std::string ownHostname, selfToken, connectedHostname;
    MTCL::HandleUser Handler{};
    static MTCL::HandleUser StaticHandler;

    std::thread *th;
    bool *continue_execution         = new bool;
    MTCL::HandleUser *HandlerPointer = new MTCL::HandleUser;

    void static waitConnect(const bool *continue_execution, int sleep_time,
                            std::unordered_map<std::string, MTCL::HandleUser> *open_connections) {

        START_LOG(gettid(), "call(sleep_time=%d)", sleep_time);

        while (*continue_execution) {
            auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));

            if (!UserManager.isValid()) {
                continue;
            }
            LOG("Handle user is valid");

            std::string connected_hostname;
            connected_hostname.reserve(HOST_NAME_MAX);
            UserManager.receive(connected_hostname.data(), HOST_NAME_MAX);

            LOG("Recived connection hostname: %s", connected_hostname.c_str());

            open_connections->insert({connected_hostname, std::move(UserManager)});
        }
    }

  public:
    explicit CapioCommunicationService(std::string proto, std::string port)
        : selfToken(proto + ":0.0.0.0:" + port) {

        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");

        ownHostname.reserve(HOST_NAME_MAX);
        gethostname(ownHostname.data(), HOST_NAME_MAX);
        LOG("My hostname is %s. Starting to listen on connection %s", ownHostname.c_str(),
            selfToken.c_str());

        std::string token_filename(ownHostname.c_str());
        token_filename +=  ".alive_connection";

        LOG("Creating alive token %s", token_filename.c_str());

        std::ofstream FilePort(token_filename);
        FilePort << port;
        FilePort.close();

        LOG("Saved self token info to FS");

        for (const auto &entry :
             std::filesystem::directory_iterator(std::filesystem::current_path())) {
            std::ifstream MyReadFile(entry.path().filename()); // apri file
            std::string TryHostName = entry.path().stem();
            std::string TryPort;
            LOG("Testing for file: %s (token: %s, tryHostName=%s)", entry.path().filename().c_str(),
                selfToken.c_str(), TryHostName.c_str());

            while (getline(MyReadFile, TryPort)) {
#ifndef CAPIO_BUILD_TESTS
                // Allow connections on loopback interface only when running tests
                if (TryHostName != ownHostname)
#endif
                    if (entry.path().extension() == ".alive_connection") {
                        // prova a connetterti
                        // std::cout << entry.path().filename().c_str();
                        std::string TryToken = "TCP:" + TryHostName + ":" + port;
                        std::cout << "TEST CONNESIONE del tipo: " << TryToken << std::endl;
                        // LOG((" IIZIO TEST CONNESIONE del tipo: " + TryToken).c_str());
                        MTCL::HandleUser UserManager = MTCL::Manager::connect(TryToken);
                        if (UserManager.isValid()) {
                            std::cout << " connesso! \n";
                            LOG("CONNNESSO");
                            connectedHostname = TryHostName;
                            Handler           = std::move(UserManager);
                            //   ConnectedHostnames[TryHostName] =std::move(UserManager);
                            //  ConnectedHostnames.insert({ownHostnameString,  Handler});

                            // Handlers.push_front(UserManager); //lavlue, ivalue different
                        } else {
                            std::cout << "non sono connesso \n";
                            // LOG("non CONNNESSO");
                        }
                    }
            }

            MyReadFile.close();
        }

        *continue_execution = true;

        MTCL::Manager::init(selfToken);
        MTCL::Manager::listen(selfToken);

        th = new std::thread(waitConnect, std::ref(continue_execution), 30, &ConnectedHostnames);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;

        // std::cout << "handler" << std::endl;
        Handler = std::move(*HandlerPointer);

        // Handler =ret.get_future().get();
        std::cout << "fine main" << std::endl;
    }

    ~CapioCommunicationService() {
        START_LOG(gettid(), "END");

        // Set the flag to stop the connection thread
        *continue_execution = false;

        while (!th->joinable()) {
            // cicla in loop finche th non e' joinabe
        }
        th->join();
        delete th;

        Handler.close();
        LOG("Handler closed.");

        std::string path = std::filesystem::current_path();
        for (const auto &entry : std::filesystem::directory_iterator(path)) {
            if (entry.path().extension() == ".txt" && (entry.path().stem() != "CMakeLists") &&
                (entry.path().stem() != "CMakeCache")) {
                std::remove(entry.path().filename().c_str());
            }
        }

        MTCL::Manager::finalize();
        LOG("Finalizing MTCL backend");

        /*
        START_LOG(gettid(), "END");

        Handler.close();
        LOG("Finalized MTCL backend");
        MTCL::Manager::finalize();
        LOG("Finalized MTCL backend");
        delete[] ownHostname;

        LOG("Finalized MTCL backend");

        std::string path = std::filesystem::current_path();
        for (const auto &entry : std::filesystem::directory_iterator(path)) {

            if (entry.path().extension() == ".txt" && (entry.path().stem() != "CMakeLists")) {
                std::remove(entry.path().filename().c_str());
            }
        }

        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;*/
    }

    std::string &recive(char *buf, uint64_t buf_size) {
        // while  Handler.probe;
        std::cout << "sono " << ownHostname << " e sono connesso con " << connectedHostname
                  << std::endl;
        START_LOG(gettid(), "inizio recv");
        LOG("Connected to %s", connectedHostname.c_str());
        if (Handler.receive(buf, buf_size) != buf_size) {
            std::cout << "errore!!" << std::endl;
            LOG("errore recv");
            MTCL_ERROR(ownHostname.c_str(), "ERROR receiving message\n");
        }
        LOG("SALTO TIME");
        /*auto endChrono            = std::chrono::system_clock::now(); // catch time
        const std::time_t endTime = std::chrono::system_clock::to_time_t(endChrono);

        void *PointerEndTime = (void *) &endTime;
        Handler.send(PointerEndTime, sizeof(endTime));*/ // send time of recived message

        return connectedHostname;
    }

    /**
     *
     * @param target
     * @param buffer
     * @param buffer_size
     * @param offset
     */
    void send(const std::string &target, char *buf, uint64_t buf_size) {
        // overdrive non serve
        std::cout << "sono " << ownHostname << " e sono connesso con " << target << "\n";

        auto startChrono            = std::chrono::system_clock::now(); // iniza timer
        const std::time_t startTime = std::chrono::system_clock::to_time_t(startChrono);
        // sleep(2);
        std::time_t *TimeEnd        = new std::time_t[1];
        if (target.compare(connectedHostname) == 0) {
            if (Handler.send(buf, buf_size) != buf_size) {
                MTCL_ERROR(ownHostname.c_str(), "ERROR sending message\n");
            } else {
                std::cout << "ho mandato: " << buf << "\n";

                Handler.receive(TimeEnd, sizeof(TimeEnd));     // rimane in attesta del tempo
                std::time_t duration = (*TimeEnd) - startTime; // tempo in secondi
                std::cout << "Il messaggio ci ha messo " << duration << " secondi \n";
                if (duration != 0) {
                    std::cout << "Hai una banda di: " << buf_size / (duration) << " Byte/s \n";
                } else {
                    std::cout << "Hai una banda altissima  \n";
                }
            }
        } else {
            std::cout << "host name non connnesso \n";
        }
    }
};

inline CapioCommunicationService *capio_communication_service;

#endif // CAPIOCOMMUNICATIONSERVICE_H
