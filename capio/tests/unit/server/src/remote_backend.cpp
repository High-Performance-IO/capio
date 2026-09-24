#include <gtest/gtest.h>

#include <cstring>

#include "utils/location.hpp"

#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/requests.hpp"

namespace {

class RecordingBackend : public Backend {
  public:
    RecordingBackend() : Backend(256) {}

    std::vector<std::string> calls;
    std::vector<std::string> targets;

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {{}, {}}; }
    void send_file(const char *message, int message_size, char *data, long int size,
                   const std::string &target) override {
        calls.emplace_back(message, static_cast<size_t>(message_size));
        calls.emplace_back(data, static_cast<size_t>(size));
        targets.push_back(target);
    }
    void recv_file(char *, long int, const std::string &) override {}
    void send_request(const char *message, int size, const std::string &target) override {
        calls.emplace_back(message, static_cast<size_t>(size));
        targets.push_back(target);
    }
    void connect_to(const std::string &) override {}
};

class ScopedBackend {
  public:
    explicit ScopedBackend(Backend *replacement) {
        EXPECT_EQ(backend, nullptr);
        backend = replacement;
    }
    ~ScopedBackend() { backend = nullptr; }
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
    EXPECT_EQ(RemoteRequest(std::string("0002"), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("0002payload"), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("xxxx payload"), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("002x payload"), "node-a").get_code(), -1);
}

TEST(RemoteRequestTest, TakesOwnershipOfHeapBuffer) {
    auto buffer = new char[13];
    std::strcpy(buffer, "0002 payload");

    RemoteRequest request(buffer, "node-a");

    EXPECT_EQ(request.get_code(), CAPIO_SERVER_REQUEST_STAT);
    EXPECT_STREQ(request.get_content(), "payload");
}

TEST(BackendTest, CompoundSendCarriesOrderedRequestAndFile) {
    RecordingBackend backend;
    char file[] = "data";

    backend.send_file("request", 7, file, 4, "node-b");

    EXPECT_EQ(backend.calls, (std::vector<std::string>{"request", "data"}));
}

TEST(RemoteRequestsTest, SerializesStatReply) {
    RecordingBackend recording_backend;
    ScopedBackend scoped_backend(&recording_backend);

    serve_remote_stat_request("/some/file", 42, 1234, false, "node-b");

    ASSERT_EQ(recording_backend.calls.size(), 1);
    EXPECT_STREQ(recording_backend.calls[0].c_str(), "0003 /some/file 42 1234 0");
    EXPECT_EQ(recording_backend.calls[0].size(),
              std::strlen(recording_backend.calls[0].c_str()) + 1);
    EXPECT_EQ(recording_backend.targets, (std::vector<std::string>{"node-b"}));
}

TEST(RemoteRequestsTest, SerializesReadReplyAndPayloadTogether) {
    RecordingBackend recording_backend;
    ScopedBackend scoped_backend(&recording_backend);
    char file[] = "data";

    serve_remote_read_request(12, 3, 4, 4, 99, true, false, "node-c", file);

    ASSERT_EQ(recording_backend.calls.size(), 2);
    EXPECT_STREQ(recording_backend.calls[0].c_str(), "0001 12 3 4 4 99 1 0");
    EXPECT_EQ(recording_backend.calls[0].size(),
              std::strlen(recording_backend.calls[0].c_str()) + 1);
    EXPECT_EQ(recording_backend.calls[1], "data");
    EXPECT_EQ(recording_backend.targets, (std::vector<std::string>{"node-c"}));
}
