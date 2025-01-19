#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include "../server/communication-service/CapioCommunicationService.hpp"
#include <thread>
#include <gtest/gtest.h>
void startFirst() {
    char recvBuff[1024];
    CapioCommunicationService first("1234");
    std::string receivedHostname = first.recive(recvBuff, 1024);
    EXPECT_STREQ(recvBuff, "ping");
    EXPECT_EQ(receivedHostname, "localhost");
}

void startSecond() {
    CapioCommunicationService second("1234");
    char buff[5]{'p', 'i', 'n', 'g', '\0'};
    second.send("fd-01", buff, sizeof(buff));
}

class CapioCommServiceTest : public ::testing::Test {

    std::thread frist_thread_;
    std::thread second_thread;

    void SetUp() override {
        frist_thread_ = std::thread(startFirst);
        std::this_thread::sleep_for(std::chrono::seconds(1));// aspetta che server parta

    }

    void TearDown() override {
        if (second_thread.joinable()) {
            second_thread.join(); //join with main and let main continue
            // (unisci il tread con il main e fai continuare il main)
        }
        if (frist_thread_.joinable()) { //jonable vuol dire che il thread ha finito e sta aspettando
                                            //di riunirsi con il main
            second_thread.join();
        }
    }
};

TEST(CapioCommServiceTest, TestNumberOne) {


}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
