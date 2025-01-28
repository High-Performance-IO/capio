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
    CapioCommunicationService first("1234", "fd-01");
    sleep(1);//aspetta che il primo si metta in wait
    CapioCommunicationService second("1234", "fd-02");
   // std::string receivedHostname = second.recive(recvBuff, 1024);
   /* EXPECT_EQ(receivedHostname, "fd-01");
    EXPECT_EQ(receivedHostname, "fd-02");*/
    //parte il thread con la funzione
 /* std::thread t1(startSecond);
    sleep(2); //aspetta che il secondo parta
    //EXPECT_STREQ(recvBuff, "ping");

    t1.join();*/






}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
