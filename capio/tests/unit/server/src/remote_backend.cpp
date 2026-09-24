#include <gtest/gtest.h>

#include "common/requests.hpp"
#include "remote/backend.hpp"

namespace {

class RecordingBackend : public Backend {
  public:
    RecordingBackend() : Backend(256) {}

    std::vector<std::string> calls;

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {{}, {}}; }
    void send_file(const char *message, int message_size, char *data, long int size,
                   const std::string &) override {
        calls.emplace_back(message, static_cast<size_t>(message_size));
        calls.emplace_back(data, static_cast<size_t>(size));
    }
    void recv_file(char *, long int, const std::string &) override {}
    void send_request(const char *message, int size, const std::string &) override {
        calls.emplace_back(message, static_cast<size_t>(size));
    }
    void connect_to(const std::string &) override {}
};

} // namespace

TEST(RemoteRequestTest, ParsesOwnedRequest) {
    RemoteRequest request(std::string("0002 payload"), "node-a");

    EXPECT_EQ(request.get_code(), CAPIO_SERVER_REQUEST_STAT);
    EXPECT_STREQ(request.get_content(), "payload");
    EXPECT_EQ(request.get_source(), "node-a");
}

TEST(RemoteRequestTest, RejectsMalformedRequest) {
    EXPECT_EQ(RemoteRequest(std::string(), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("0002payload"), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("xxxx payload"), "node-a").get_code(), -1);
}

TEST(BackendTest, CompoundSendCarriesOrderedRequestAndFile) {
    RecordingBackend backend;
    char file[] = "data";

    backend.send_file("request", 7, file, 4, "node-b");

    EXPECT_EQ(backend.calls, (std::vector<std::string>{"request", "data"}));
}
