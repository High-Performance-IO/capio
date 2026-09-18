#include "common/constants.hpp"
#include "remote/backend.hpp"

#include "calf/StlLogger.h"

RemoteRequest::RemoteRequest(std::string buf_recv, std::string source)
    : _source(std::move(source)) {
    START_LOG(gettid(), "call(buf_recv=%s, source=%s)", buf_recv.c_str(), _source.c_str());
    if (buf_recv.size() < 5 || buf_recv[4] != ' ') {
        return;
    }

    const auto [ptr, ec] = std::from_chars(buf_recv.data(), buf_recv.data() + 4, _code);
    if (ec != std::errc() || ptr != buf_recv.data() + 4) {
        _code = -1;
        return;
    }

    _content = buf_recv.substr(5);
    LOG("Received request %d from %s : %s", _code, _source.c_str(), _content.c_str());
}

RemoteRequest::RemoteRequest(char *buf_recv, const std::string &source)
    : RemoteRequest(buf_recv == nullptr ? std::string{} : std::string(buf_recv), source) {
    delete[] buf_recv;
}

const std::string &RemoteRequest::get_source() const { return this->_source; }
[[nodiscard]] const char *RemoteRequest::get_content() const { return this->_content.c_str(); }
[[nodiscard]] int RemoteRequest::get_code() const { return this->_code; }
