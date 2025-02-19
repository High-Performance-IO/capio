#ifndef CAPIOBACKEND_HPP
#define CAPIOBACKEND_HPP

#include "capio/constants.hpp"

#include <mtcl.hpp>

class NotImplementedBackendMethod : public std::exception {
  public:
    [[nodiscard]] const char *what() const noexcept override {
        auto msg = new char[1024]{};
        sprintf(msg, "The chosen backend does not implement method: %s", __func__);
        return msg;
    }
};

class BackendInterface {
  public:
    virtual ~BackendInterface() = default;
    /**
     * @brief Send data to target
     *
     * @param target Hostname of remote target
     * @param buf pointer to data to sent
     * @param buf_size length of@param filepath
     * @param start_offset
     * @param buf
     */
    virtual void send(const std::string &target, char *buf, uint64_t buf_size,
                      const std::string &filepath, capio_off64_t start_offset) {
        throw NotImplementedBackendMethod();
    };

    /**
     * @brief recive data
     *
     * @param buf allocated data buffer
     * @param buf_size size of @param buf
     * @param start_offset
     * @return std::string hostname of sender
     */
    virtual std::string &recive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) {
        throw NotImplementedBackendMethod();
    };

    /**
     * 
     * @return A vector of hostnames for which a connection exists
     */
    virtual std::vector<std::string> get_open_connections() { throw NotImplementedBackendMethod(); }
};

#endif // CAPIOBACKEND_HPP
