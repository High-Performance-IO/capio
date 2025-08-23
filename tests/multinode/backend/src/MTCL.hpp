#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include <capio/constants.hpp>
#include <communication-service/CapioCommunicationService.hpp>
#include <gtest/gtest.h>
#include <thread>

constexpr char TEST_MESSAGE[]        = "hello world how is it going?";
constexpr capio_off64_t BUFFER_SIZES = 1024;

TEST(CapioCommServiceTest, TestPingPong) {
    START_LOG(gettid(), "INFO: TestPingPong");
    const int port             = 1234;
    std::string proto          = "TCP";
    auto communication_service = new CapioCommunicationService(proto, port, "multicast");
    capio_off64_t size_revc, offset;

    std::vector<std::string> connections;

    do {
        connections = capio_backend->get_open_connections();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    } while (connections.empty());

    char ownHostname[HOST_NAME_MAX] = {0};
    gethostname(ownHostname, HOST_NAME_MAX);

    for (const auto &i : connections) {
        if (i.compare(ownHostname) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Sending ping to: " << i << std::endl;
            char buff[BUFFER_SIZES]{0}, buff1[BUFFER_SIZES]{0};
            memcpy(buff, TEST_MESSAGE, strlen(TEST_MESSAGE));
            capio_backend->send(i, buff, BUFFER_SIZES, "./test", 0);
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "sent ping to: " << i
                      << ". Waiting for response" << std::endl;
            capio_backend->receive(buff1, &size_revc, &offset);
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Received ping response from : " << i
                      << std::endl;
            EXPECT_EQ(strcmp(buff, buff1), 0);
            delete communication_service;
            return;
        }
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Listening for ping from: " << i
                  << std::endl;
        char recvBuff[BUFFER_SIZES];
        capio_backend->receive(recvBuff, &size_revc, &offset);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Received ping from: " << i << std::endl;
        EXPECT_EQ(strcmp(recvBuff, TEST_MESSAGE), 0);
        capio_backend->send(i, recvBuff, size_revc, "./test", 0);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Sent ping response to: " << i << std::endl;
        delete communication_service;
        return;
    }
}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
