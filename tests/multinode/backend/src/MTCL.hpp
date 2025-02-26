#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include <capio/constants.hpp>
#include <communication-service/BackendInterface.hpp>
#include <communication-service/MTCL_backend.hpp>
#include <gtest/gtest.h>
#include <thread>

constexpr char TEST_MESSAGE[]        = "hello world how is it going?";
constexpr capio_off64_t BUFFER_SIZES = 1024;

TEST(CapioCommServiceTest, TestPingPong) {
    START_LOG(gettid(), "INFO: TestPingPong");
    gethostname(node_name.data(), HOST_NAME_MAX);
    MTCL_backend backend("TCP", "1234", 300);
    capio_off64_t size_revc, offset;

    std::vector<std::string> connections;

    do {
        connections = backend.get_open_connections();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    } while (connections.empty());

    char ownHostname[HOST_NAME_MAX] = {0};
    gethostname(ownHostname, HOST_NAME_MAX);

    for (const auto &i : connections) {
        if (i.compare(ownHostname) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Sending ping to: " << i << std::endl;
            char buff[BUFFER_SIZES]{0}, buff1[BUFFER_SIZES]{0};
            memcpy(buff, TEST_MESSAGE, strlen(TEST_MESSAGE));
            backend.send(i, buff, BUFFER_SIZES, "./test", 0);
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "sent ping to: " << i
                      << ". Waiting for response" << std::endl;
            backend.receive(buff1, &size_revc, &offset);
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Received ping response from : " << i
                      << std::endl;
            EXPECT_EQ(strcmp(buff, buff1), 0);
            return;
        }
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Listening for ping from: " << i
                  << std::endl;
        char recvBuff[BUFFER_SIZES];
        backend.receive(recvBuff, &size_revc, &offset);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Received ping from: " << i << std::endl;
        EXPECT_EQ(strcmp(recvBuff, TEST_MESSAGE), 0);
        backend.send(i, recvBuff, size_revc, "./test", 0);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Sent ping response to: " << i << std::endl;
        return;
    }
}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
