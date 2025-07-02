#ifndef CAPIOBACKEND_HPP
#define CAPIOBACKEND_HPP

#include "capio/constants.hpp"

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
     * @brief receive data
     *
     * @param buf allocated data buffer
     * @param buf_size size of @param buf
     * @param start_offset
     * @return std::string hostname of sender
     */
    virtual std::string receive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) {
        throw NotImplementedBackendMethod();
    };

    /**
     *
     * @return A vector of hostnames for which a connection exists
     */
    virtual std::vector<std::string> get_open_connections() { throw NotImplementedBackendMethod(); }
};

/*
 * This class implements a placeholder for backend interface, whenever CAPIO is only providing IO
 * coordination
 */
class NoBackend final : public BackendInterface {
  public:
    void send(const std::string &target, char *buf, uint64_t buf_size, const std::string &filepath,
              capio_off64_t start_offset) override {
        return;
    };

    std::string receive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) override {
        return {"no-backend"};
    }

    std::vector<std::string> get_open_connections() override { return {}; }
};

inline BackendInterface *capio_backend;

#endif // CAPIOBACKEND_HPP
