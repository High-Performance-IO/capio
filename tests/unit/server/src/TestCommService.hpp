#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include "../server/communication-service/CapioCommunicationService.hpp"
#include <thread>
#include <gtest/gtest.h>


void startSecond() {
    CapioCommunicationService second("1234", "fd-02");
    char buff[5]{'p', 'i', 'n', 'g', '\0'};
    second.send("fd-01", buff, sizeof(buff));
}


TEST(CapioCommServiceTest, TestNumberOne) {
    //pare il il primo utente che fara da server
     char recvBuff[1024];
    sleep(3);
    CapioCommunicationService first("1234", "fd-01");
    sleep(3);//aspetta che il primo si metta in wait
   // CapioCommunicationService second("1234", "fd-02");
    sleep(3);
   // std::string receivedHostname = second.recive(recvBuff, 1024);



    // Buffer to receive message
  /*  char recvBuff[1024];

    // Initialize the first instance (acting as server)
    CapioCommunicationService first("1234", "fd-01");

    // Give some time for the first instance to initialize and wait for connections
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Start the second instance (acting as client) in a separate thread
    std::thread t1(startSecond);

    // Receive message in the first instance
    std::string receivedHostname = first.recive(recvBuff, 1024);

    t1.join();  // Ensure the second instance thread completes

    // Check the received message and hostname
    EXPECT_STREQ(recvBuff, "ping");
    EXPECT_EQ(receivedHostname, "fd-02");*/






}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
