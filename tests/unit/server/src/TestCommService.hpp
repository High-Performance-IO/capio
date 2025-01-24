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
    CapioCommunicationService first("3456", "fd-02");
   /* std::string receivedHostname = first.recive(recvBuff, 1024);
    //parte il thread con la funzione
  std::thread t1(startSecond);
    sleep(2); //aspetta che il secondo parta
    //EXPECT_STREQ(recvBuff, "ping");
    EXPECT_EQ(receivedHostname, "fd-02");
    t1.join();*/






}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
