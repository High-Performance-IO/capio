#ifndef CAPIO_REQUEST_HANDLERS_HPP
#define CAPIO_REQUEST_HANDLERS_HPP

#include "common/requests.hpp"
#include "utils/types.hpp"

class RequestHandler {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers{};

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table();

  public:
    RequestHandler();
    ~RequestHandler();

    void start() const;
};

#endif // CAPIO_REQUEST_HANDLERS_HPP
