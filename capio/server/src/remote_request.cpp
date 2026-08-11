#include "common/constants.hpp"
#include "remote/backend.hpp"

#include "calf/StlLogger.h"

RemoteRequest::RemoteRequest(char *buf_recv, const std::string &source) : _source(source) {
    START_LOG(gettid(), "call(buf_recv=%s, source=%s)", buf_recv, source.c_str());
    int code;
    auto [ptr, ec] = std::from_chars(buf_recv, buf_recv + 4, code);
    if (ec == std::errc()) {
        this->_code     = code;
        this->_buf_recv = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
        strcpy(this->_buf_recv, ptr + 1);
        LOG("Received request %d from %s : %s", this->_code, this->_source.c_str(),
            this->_buf_recv);
    } else {
        this->_code = -1;
    }
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
