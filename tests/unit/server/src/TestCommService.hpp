#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include "../server/communication-service/CapioCommunicationService.hpp"
#include <gtest/gtest.h>
#include <thread>

constexpr char TEST_MESSAGE[] = "hello world how is it going?";
constexpr capio_off64_t BUFFER_SIZES = 1024;

TEST(CapioCommServiceTest, TestNumberOne) {
    // pare il il primo utente che fara da server
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    CapioCommunicationService backend("TCP", "1234", 300);
    capio_off64_t size_revc, offset;
    auto connections = backend.get_open_connections();

    char ownHostname[HOST_NAME_MAX] = {0};
    gethostname(ownHostname, HOST_NAME_MAX);

    for (auto i : connections) {
        std::cout << i << ownHostname;

        if (i.compare(ownHostname) < 0) {
            // std::cout << i << ownHostname;
            char buff[BUFFER_SIZES]{0};
            memcpy(buff, TEST_MESSAGE, strlen(TEST_MESSAGE));
            backend.send(i, buff, BUFFER_SIZES, "./test", 0);
            backend.recive(buff, &size_revc, &offset);
            return;
        }
        char recvBuff[1024];
        backend.recive(recvBuff, &size_revc, &offset);
        backend.send(i, recvBuff, size_revc, "./test", 0);
        return;
    }
}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
