#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include "../server/communication-service/CapioCommunicationService.hpp"
#include <gtest/gtest.h>
#include <thread>
/*
void startSecond() {
    CapioCommunicationService second("1234", "fd-02");
    char buff[5]{'p', 'i', 'n', 'g', '\0'};
     second.send("fd-01", buff, sizeof(buff));
}*/

TEST(CapioCommServiceTest, TestNumberOne) {
    // pare il il primo utente che fara da server
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);

    char recvBuff[1024];
    CapioCommunicationService backend("TCP","1234");
    //sleep(3); // aspetta che il primo si metta in wait

    const auto other_hostname = std::string("fd-06");

    if (std::string(hostname) == "fd-05") { //fd-05 fa una send a fd-06
        std::cout << "prova send \n";
       // sleep(30);
        backend.send(other_hostname, "Ciao tests 1234", 1024);
    } else { //chiunque altro fa una recive
       std::cout << "provaaaaa receive \n";
        sleep(30); //continuiamo a lasciare in funzione il thread in listen aspettando una connessione
       // std::string receivedHostname = backend.recive(recvBuff, 1024);

        std::cout << "exit reveive" << std::endl;

        //EXPECT_EQ(receivedHostname, "fd-05");
    }

    // Buffer to receive message
    /*  char recvBuff[1024];

      // Initialize the first instance (acting as server)
      CapioCommunicationService first("1234", "fd-01");

      // Give some time for the first instance to initialize and wait for connections
      //std::this_thread::sleep_for(std::chrono::seconds(3));

      // Start the second instance (acting as client) in a separate thread
      std::thread t1(startSecond);
      sleep(3);
      t1.join();
      // Receive message in the first instance
      std::string receivedHostname = first.recive(recvBuff, 1024);

       // Ensure the second instance thread completes

      // Check the received message and hostname
     // EXPECT_STREQ(recvBuff, "ping");
      EXPECT_EQ(receivedHostname, "fd-02");*/
}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
