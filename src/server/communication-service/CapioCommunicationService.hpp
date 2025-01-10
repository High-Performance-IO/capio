#ifndef CAPIOCOMMUNICATIONSERVICE_H
#define CAPIOCOMMUNICATIONSERVICE_H
#include "BackendInterface.hpp"
#include "capio/logger.hpp"

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
class CapioCommunicationService {

  private:
    MTCL::HandleUser Handler{};
    std::unordered_map<std::string, int> ConnectedHostnames; // mappa con hostname connesso e port
    char *ownHostname = new char[HOST_NAME_MAX];
    std::string ownHostnameString;
    std::string connectedHostname;
    std::thread *th;

    bool *continue_execution = new bool;

    //
    static void waitConnect(const bool *continue_execution, std::string Token) {
        START_LOG(gettid(), "INFO: instance of CapioCommunicationService");
        MTCL::Manager::listen(Token);
        MTCL::HandleUser UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));
        while (*continue_execution) {
            UserManager = MTCL::Manager::getNext(std::chrono::microseconds(30));
            if (!UserManager.isValid()) {
                continue;
            }
            std::cout << "server connesso! \n";
            break;
        }
    }

  public: // in questa versione hotname in input non serve
    explicit CapioCommunicationService(std::string port) {

        // scrivi toke su file
        gethostname(ownHostname, HOST_NAME_MAX);
        ownHostnameString   = ownHostname;
        std::string MyToken = "TCP:" + ownHostnameString + ":" + port;
        std::string path    = std::filesystem::current_path();
        std::ofstream FilePort(ownHostnameString + ".txt");
        FilePort << port;
        FilePort.close();

        // connettiti con tutti i file diponibili
        for (const auto &entry : std::filesystem::directory_iterator(path)) {

            std::ifstream MyReadFile(entry.path().filename()); // apri file
            std::string TryHostName = entry.path().stem();
            std::string TryPort;

            while (getline(MyReadFile, TryPort)) { // SALVA PORTA
                // NON FUNZIONA IN LOCAL
                if (entry.path().extension() == ".txt" && (TryHostName != ownHostnameString)) {
                    // prova a connetterti
                    MTCL::HandleUser UserManager =
                        MTCL::Manager::connect("TCP:" + TryHostName + TryPort);
                }
            }

            MyReadFile.close();
        }

        // rimani in attesa di connections
        START_LOG(gettid(), "call()");
        *continue_execution = true;
        th                  = new std::thread(waitConnect, std::ref(continue_execution), MyToken);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCommunicationService initialization completed." << std::endl;
    }

    ~CapioCommunicationService() {
        START_LOG(gettid(), "call()");

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

    virtual std::string &recive(char *buf, uint64_t buf_size) {
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
    virtual void send(const std::string &target, char *buf, uint64_t buf_size) {
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
