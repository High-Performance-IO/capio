#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include "capio/logger.hpp" //se lo tongo non vann i log
#include <future>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <mtcl.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// CTRL + ALT + L PER FORMAT TEXTO SHIFT
class CapioCommunicationService : BackendInterface {
    // CapioCommunicationService impemetns
    // interface

private:
    //std::pmr::list<MTCL::HandleUser> Handlers ; //array of handlers
    std::unordered_map<std::string, MTCL::HandleUser> ConnectedHostnames; // mappa con hostname connesso e port
    char *ownHostname = new char[HOST_NAME_MAX];
    std::string ownHostnameString;
    std::string connectedHostname;
    MTCL::HandleUser Handler{};
    static MTCL::HandleUser StaticHandler;
    //std::promise<MTCL::HandleUser> ret;
    //std::promise<MTCL::HandleUser> *retPointer;

    std::thread *th;
    bool *continue_execution = new bool;
    MTCL::HandleUser *HandlerPointer = new MTCL::HandleUser;

    //
    static void waitConnect(bool *continue_execution, std::string Token, MTCL::HandleUser *handlepointer) {

        std::cout << "RIMANI IN ATTESA FI UNA CONNESIONE EL TIPO: " << Token <<std::endl;

        START_LOG(gettid(), (("rimani in attesa di una connection del tipo: " + Token).c_str()));

        MTCL::Manager::init(Token);
        MTCL::Manager::listen(Token);


        MTCL::HandleUser UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));
        while (*continue_execution ) {
            /* MTCL::HandleUser*/
            UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));

            if (!UserManager.isValid()) {
                std::cout << "no valid";
                continue;
            }

            *handlepointer = std::move(UserManager);
            // r->set_value_at_thread_exit(UserManager);
            //StaticHandler = std::move(UserManager);
            std::cout << "server connesso!" << std::endl;
            LOG(" server connesso! \n");
            break;
        }

        std::cout << "fine WAIT CONNECTION ";
        LOG("  finished waiting for connections \n");

    }

public:
    explicit CapioCommunicationService(std::string port) {
        // hostname in input scritto solo per test hardcode

        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");
        /*if (MTCL::Manager::init(port) != 0) { //manager already initialized
            LOG("ERRORE");
        }*/
        // scrivi toke su file
        gethostname(ownHostname, HOST_NAME_MAX);
        ownHostnameString = ownHostname;
        //ownHostnameString = own;
        std::string MyToken = "TCP:" + ownHostnameString + ":" + port;
        std::cout << "sono " << ownHostnameString << "\n";
        LOG(MyToken.c_str());
        std::string path = std::filesystem::current_path();
        std::ofstream FilePort(ownHostnameString + ".txt");
        FilePort << port;
        FilePort.close();

        //CREATO FILE CON TOKEN INFO

        // connettiti con tutti i file diponibili
        //APRI PULL REQUEST COMMUNICATION-SERVICE NON AGGIUNGERE
        for (const auto &entry: std::filesystem::directory_iterator(path)) {
            std::ifstream MyReadFile(entry.path().filename()); // apri file
            std::string TryHostName = entry.path().stem();
            std::string TryPort;
            // LOG(entry.path().filename().c_str());
            // LOG(MyToken.c_str());
            // LOG(TryHostName.c_str());

            while (getline(MyReadFile, TryPort)) {
                // SALVA PORTA
                // NON FUNZIONA IN LOCAL

#ifndef CAPIO_BUILD_TESTS
                // Allow connections on loopback interface only when running tests
                if (TryHostName != ownHostnameString)
#endif

                    if (entry.path().extension() == ".txt" && TryHostName != "CMakeLists" && TryHostName !=
                        "CMakeCache") {
                        // prova a connetterti
                        // std::cout << entry.path().filename().c_str();
                        std::string TryToken = "TCP:" + TryHostName + ":" + port;
                        std::cout << "TEST CONNESIONE del tipo: " << TryToken  << std::endl;
                        // LOG((" IIZIO TEST CONNESIONE del tipo: " + TryToken).c_str());
                        MTCL::HandleUser UserManager = MTCL::Manager::connect(TryToken);
                        if (UserManager.isValid()) {
                            std::cout << " connesso! \n";
                            LOG("CONNNESSO");
                            connectedHostname = TryHostName;
                            Handler = std::move(UserManager);
                            //   ConnectedHostnames[TryHostName] =std::move(UserManager);
                            //  ConnectedHostnames.insert({ownHostnameString,  Handler});

                            //Handlers.push_front(UserManager); //lavlue, ivalue different
                        } else {
                            std::cout << "non sono connesso \n";
                            // LOG("non CONNNESSO");
                        }
                    }
            }

            MyReadFile.close();
        }

        // rimani in attesa di connections
        // std::future<MTCL::HandleUser> returnFuture= ret.get_future();
        *continue_execution = true;

        /*&ret*/
        th = new std::thread(waitConnect, std::ref(continue_execution), MyToken, std::ref(HandlerPointer));
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                << "CapioCommunicationService initialization completed." << std::endl;

        //std::cout << "handler" << std::endl;
        Handler = std::move(*HandlerPointer);

        // Handler =ret.get_future().get();
        std::cout << "fine main"  << std::endl;
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
        for (const auto &entry: std::filesystem::directory_iterator(path)) {
            if (entry.path().extension() == ".txt" && (entry.path().stem() != "CMakeLists") && (
                    entry.path().stem() != "CMakeCache")) {
                std::remove(entry.path().filename().c_str());
            }
        }

        MTCL::Manager::finalize();
        LOG("Finalizing MTCL backend");
        delete[] ownHostname;
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
        std::cout << "sono " << ownHostnameString << " e sono connesso con " << connectedHostname << std::endl;
        START_LOG(gettid(), "inizio recv");
        LOG(("sono " + ownHostnameString + " e sono connesso con " + connectedHostname).c_str());
        if (Handler.receive(buf, buf_size) != buf_size) {
            std::cout << "errore!!" << std::endl;
            LOG("errore recv");
            MTCL_ERROR(ownHostname, "ERROR receiving message\n");
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
        std::cout << "sono " << ownHostnameString << " e sono connesso con " << target << "\n";

        auto startChrono = std::chrono::system_clock::now(); // iniza timer
        const std::time_t startTime = std::chrono::system_clock::to_time_t(startChrono);
        // sleep(2);
        std::time_t *TimeEnd = new std::time_t[1];
        if (target.compare(connectedHostname) == 0) {
            if (Handler.send(buf, buf_size) != buf_size) {
                MTCL_ERROR(ownHostname, "ERROR sending message\n");
            } else {
                std::cout << "ho mandato: " << buf << "\n";

                Handler.receive(TimeEnd, sizeof(TimeEnd)); // rimane in attesta del tempo
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
