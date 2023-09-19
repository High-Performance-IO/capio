#ifndef CAPIO_SERVER_REQUESTS_HPP
#define CAPIO_SERVER_REQUESTS_HPP

#include <charconv>

#include "capio/requests.hpp"
#include "types.hpp"

CSBufRequest_t* buf_requests;
CSBufResponse_t* bufs_response;

/**
* Initialize request and response buffers
* @return
*/
inline void init_server() {
    //TODO: replace number with constexpr
    buf_requests = new CSBufRequest_t(
            "circular_buffer",
            1024 * 1024,
            CAPIO_REQUEST_MAX_SIZE,
            CAPIO_SEM_TIMEOUT_NANOSEC,
            CAPIO_SEM_RETRIES);
    bufs_response = new CSBufResponse_t();
}

/**
 * Delete request and response buffers
 * @return
 */
inline void destroy_server() {
    buf_requests->free_shm();
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "buf_requests cleanup completed"<<std::endl;

    for (auto& pair : *bufs_response) {
        pair.second->free_shm();
        delete pair.second;
    }
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << "buf_response cleanup completed"<<std::endl;
}

/**
* Add a new response buffer for thread @param tid
* @param tid
* @return
*/
inline void register_listener(long tid) {
    //TODO: replace numbers with constexpr
    auto *p_buf_response = new Circular_buffer<off_t>(
            "buf_response_" + std::to_string(tid),
            8 * 1024 * 1024,
            sizeof(off_t),
            CAPIO_SEM_TIMEOUT_NANOSEC,
            CAPIO_SEM_RETRIES);
    bufs_response->insert(std::make_pair(tid, p_buf_response));
}

/**
 * Delete the response buffer associated with thread @param tid
 * @param tid
 * @return
 */
inline void remove_listener(int tid){
    auto it_resp = bufs_response->find(tid);
    if (it_resp != bufs_response->end()) {
        it_resp->second->free_shm();
        delete it_resp->second;
        bufs_response->erase(it_resp);
    }
}

/**
 * Read next incoming request into @param str and returns the request code
 * @param str
 * @return request code
 */
inline auto read_next_request(char *str){
    char req[CAPIO_REQUEST_MAX_SIZE];
    buf_requests->read(req);
    START_LOG(gettid(), "call(req=%s)", req);
    int code;
    auto [ptr, ec] = std::from_chars(req, req + 4, code);
    if (ec == std::errc()) {
        strcpy(str, ptr+1);
        return code;
    } else {
        ERR_EXIT("Invalid request %d%s", code, ptr);
    }
}

/**
 * Write offset to response buffer of process @param tid
 * @param tid
 * @param offset
 * @return
 */
inline auto write_response(int tid, off64_t offset){
    START_LOG(gettid(), "call(tid=%d, offset=%ld)", tid, offset);

    return bufs_response->at(tid)->write(&offset);
}

inline void stat_reply_request(const char *const path, off64_t size, int dir) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %s %ld %d", CAPIO_REQUEST_STAT_REPLY, path, size, dir);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

#endif //CAPIO_SERVER_REQUESTS_HPP
