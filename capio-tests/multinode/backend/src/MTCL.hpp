#ifndef TEST_CAPIOCOMMUNICATIONSERVICE_HPP
#define TEST_CAPIOCOMMUNICATIONSERVICE_HPP

#include <capio/constants.hpp>
#include <capio/logger.hpp>
#include <gtest/gtest.h>
#include <include/communication-service/capio_communication_service.hpp>
#include <include/communication-service/data-plane/backend_interface.hpp>
#include <thread>

const char *filename   = "data.bin";
const int chunkSize = 1024;
const int totalSize = 2048;

inline int writer() {

    char buffer[chunkSize];
    for (int i = 0; i < chunkSize; i++) {
        buffer[i] = i % 26 + 'A';
    }

    FILE *fp = fopen(filename, "wb");
    EXPECT_NE(fp, nullptr);

    for (int i = 0; i < totalSize / chunkSize; i++) {
        EXPECT_EQ(fwrite(buffer, 1, chunkSize, fp), chunkSize);
    }

    fclose(fp);
    printf("Wrote %d bytes to %s\n", totalSize, filename);
    return 0;
}

inline int reader() {

    char buffer[chunkSize];
    size_t totalRead = 0;

    FILE *fp = fopen(filename, "rb");
    EXPECT_NE(fp, nullptr);

    // Read in 1024-byte chunks until EOF
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, chunkSize, fp)) > 0) {
        totalRead += bytesRead;
    }

    EXPECT_EQ(totalRead, totalSize);

    fclose(fp);
    printf("Total read: %zu bytes\n", totalRead);
    return 0;
}

TEST(CapioCommServiceTest, TestCapioMemoryNetworkBackend) {

    if (const auto program = std::getenv("APP_TYPE"); std::string(program) == "writer") {
        EXPECT_EQ(writer(), 0);
    } else {
        EXPECT_EQ(reader(), 0);
    }
}

#endif // TEST_CAPIOCOMMUNICATIONSERVICE_HPP
