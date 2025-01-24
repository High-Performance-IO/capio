#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include "capio/logger.hpp" //se lo tongo non vann i log

#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mtcl.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
// CTRL + ALT + L PER FORMAT TEXTO SHIFT
class CapioCommunicationService : BackendInterface { //CapioCommunicationService impemetns interface

  private:
    MTCL::HandleUser Handler{};
    std::unordered_map<std::string, int> ConnectedHostnames; // mappa con hostname connesso e port
    char *ownHostname = new char[HOST_NAME_MAX];
    std::string ownHostnameString;
    std::string connectedHostname;
    std::thread *th;

    bool *continue_execution = new bool;

    //
    static void waitConnect( bool *continue_execution, std::string Token) {
        START_LOG(gettid(), "rimani in attesa");
        MTCL::Manager::listen(Token);
        MTCL::HandleUser UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));
        while (*continue_execution) {
            UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));
            if (!UserManager.isValid()) {
                continue;
            }
            //std::cout << "server connesso! \n";
            LOG(" server connesso! \n");
            //break;
        }
    }

  public:




    explicit CapioCommunicationService(std::string port, std::string own) {// hostname in input scritto solo per test hardcode
        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");
        // scrivi toke su file
        gethostname(ownHostname, HOST_NAME_MAX);
       // ownHostnameString   = ownHostname;
        ownHostnameString = own;

        std::string MyToken = "TCP:" + ownHostnameString + ":" + port;
        LOG(MyToken.c_str());
        std::string path    = std::filesystem::current_path();
        std::ofstream FilePort(ownHostnameString + ".txt");
        FilePort << port;
        FilePort.close();

        // connettiti con tutti i file diponibili
        for (const auto &entry : std::filesystem::directory_iterator(path)) {

            std::ifstream MyReadFile(entry.path().filename()); // apri file
            std::string TryHostName = entry.path().stem();
            std::string TryPort;
            LOG(entry.path().filename().c_str());
            LOG(MyToken.c_str());
            LOG(TryHostName.c_str());

            LOG(" INIZIO TEST CONNESIONE \n");
            while (getline(MyReadFile, TryPort)) { // SALVA PORTA
                // NON FUNZIONA IN LOCAL
                if (entry.path().extension() == ".txt" && (TryHostName != ownHostnameString)) {
                    // prova a connetterti
                    MTCL::HandleUser UserManager =
                        MTCL::Manager::connect("TCP:" + TryHostName + ":" + TryPort);
                }
            }

            MyReadFile.close();
        }

        // rimani in attesa di connections
        LOG( "Waiting for connections");
        *continue_execution = true;
        th                  = new std::thread(waitConnect, std::ref(continue_execution), MyToken);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;
    }

    ~CapioCommunicationService() {
        START_LOG(gettid(), "END");

        Handler.close();
        MTCL::Manager::finalize();
        delete[] ownHostname;

        LOG("Finalized MTCL backend");

        std::string path = std::filesystem::current_path();
        for (const auto &entry : std::filesystem::directory_iterator(path)) {

            if (entry.path().extension() == ".txt") {
                std::remove(entry.path().filename().c_str());
            }
        }

        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete th;
        delete continue_execution;
    }

    std::string &recive(char *buf, uint64_t buf_size) { //overdrive non serve
        std::cout << "sono " << ownHostnameString << " e sono connesso con " << connectedHostname
                  << "\n";

        if (Handler.receive(buf, buf_size) != buf_size) {
            MTCL_ERROR(ownHostname, "ERROR receiving message\n");
        }
        auto endChrono            = std::chrono::system_clock::now(); // catch time
        const std::time_t endTime = std::chrono::system_clock::to_time_t(endChrono);

        void *PointerEndTime = (void *) &endTime;
        Handler.send(PointerEndTime, sizeof(endTime)); // send time of recived message

        return connectedHostname;
    }

    /**
     *
     * @param target
     * @param buffer
     * @param buffer_size
     * @param offset
     */
    void send(const std::string &target, char *buf, uint64_t buf_size) { //overdrive non serve
        std::cout << "sono " << ownHostnameString << " e sono connesso con " << buf_size << "\n";

        auto startChrono            = std::chrono::system_clock::now(); // iniza timer
        const std::time_t startTime = std::chrono::system_clock::to_time_t(startChrono);
        // sleep(2);
        std::time_t *TimeEnd        = new std::time_t[1];
        if (target.compare(connectedHostname) == 0) {
            if (Handler.send(buf, buf_size) != buf_size) {
                MTCL_ERROR(ownHostname, "ERROR sending message\n");
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
