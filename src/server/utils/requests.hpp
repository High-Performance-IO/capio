#ifndef CAPIO_SERVER_REQUESTS_HPP
#define CAPIO_SERVER_REQUESTS_HPP

#include <charconv>

#include "capio/requests.hpp"
#include "capio/types.hpp"

CSBufRequest_t *buf_requests;
CSBufResponse_t *bufs_response;

/**
 * Initialize request and response buffers
 * @return
 */
inline void init_server() {
    buf_requests  = new CSBufRequest_t(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE,
                                       workflow_name);
    bufs_response = new CSBufResponse_t();
}

/**
 * Delete request and response buffers
 * @return
 */
inline void destroy_server() {
    delete buf_requests;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_requests cleanup completed"
              << std::endl;

    delete bufs_response;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_response cleanup completed"
              << std::endl;
}

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_listener(long tid) {
    // TODO: replace numbers with constexpr
    auto *p_buf_response =
        new CircularBuffer<off_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT,
                                  sizeof(off_t), workflow_name);
    bufs_response->insert(std::make_pair(tid, p_buf_response));
}

/**
 * Delete the response buffer associated with thread @param tid
 * @param tid
 * @return
 */
inline void remove_listener(int tid) {
    auto it_resp = bufs_response->find(tid);
    if (it_resp != bufs_response->end()) {
        delete it_resp->second;
        bufs_response->erase(it_resp);
    }
}

/**
 * Read next incoming request into @param str and returns the request code
 * @param str
 * @return request code
 */
inline auto read_next_request(char *str) {
    char req[CAPIO_REQ_MAX_SIZE];
    buf_requests->read(req);
    START_LOG(gettid(), "call(req=%s)", req);
    int code       = -1;
    auto [ptr, ec] = std::from_chars(req, req + 4, code);
    if (ec == std::errc()) {
        strcpy(str, ptr + 1);
    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                  << std::endl;
        ERR_EXIT("Invalid request %d%s", code, ptr);
    }
    return code;
}

/**
 * Write offset to response buffer of process @param tid
 * @param tid
 * @param offset
 * @return
 */
inline void write_response(int tid, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, offset=%ld)", tid, offset);

    return bufs_response->at(tid)->write(&offset);
}

#endif // CAPIO_SERVER_REQUESTS_HPP
