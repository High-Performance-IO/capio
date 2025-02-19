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
    CapioCommunicationService backend("TCP", "1234", 300);

    uint64_t size_send = 1024;
    capio_off64_t size_revc;
    capio_off64_t offset;
    std::vector<std::string> connections;
    connections = backend.get_open_connections();
    char ownHostname[HOST_NAME_MAX] = {0};
    gethostname(ownHostname, HOST_NAME_MAX);
    for (auto i: connections) {
        std::cout << i << ownHostname;

        if (i.compare(ownHostname) < 0) {
            //std::cout << i << ownHostname;
            char buff[5]{'p', 'i', 'n', 'g', '\0'};
          backend.send(i, buff, size_send, "./test", 0);
            backend.recive(buff, &size_revc, &offset);
            return;
        }
        char recvBuff[1024];
       /* std::string recivedPath =*/ backend.recive(recvBuff, &size_revc, &offset);
     //   std::cout << recvBuff;
       // std::cout << recivedPath;
        backend.send(i, recvBuff, size_revc, "./test", 0);
        return;
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
