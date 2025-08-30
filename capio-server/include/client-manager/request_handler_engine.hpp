#ifndef CAPIO_CL_ENGINE_MAIN_HPP
#define CAPIO_CL_ENGINE_MAIN_HPP

#include "capio/requests.hpp"

#include <include/utils/types.hpp>

/**
 * @brief Class that handles the system calls received from the posix client application
 *
 */
class RequestHandlerEngine {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers{};
    CSBufRequest_t *buf_requests;

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table();

    /**
     * Read next incoming request into @param str and returns the request code
     * @param str
     * @return request code
     */
    inline auto read_next_request(char *str) const;

  public:
    explicit RequestHandlerEngine();

    ~RequestHandlerEngine();

    /**
     * @brief Start the main loop on the main thread that will read each request one by one from all
     * the posix clients (aggregated) and handle the response
     *
     */
    void start() const;
};

inline RequestHandlerEngine *request_handlers_engine;

#endif // CAPIO_CL_ENGINE_MAIN_HPP