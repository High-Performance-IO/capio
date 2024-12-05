#ifndef CAPIOBACKEND_HPP
#define CAPIOBACKEND_HPP

#include <mtcl.hpp>

class NotImplementedBackendMethod : public std::exception {
  public:
    const char *what() const noexcept override {
        auto msg = new char[1024]{};
        sprintf(msg, "The chosen backend does not implement method: %s", __func__);
        return msg;
    }
};

class BackendInterface {
public:
    /**
     * @brief Send data to target
     *
     * @param target Hostname of remote target
     * @param buf pointer to data to sent
     * @param buf_size length of @param buf
     */
    virtual void send(const std::string &target, char *buf, uint64_t buf_size) {
        throw NotImplementedBackendMethod();
    };

    /**
     * @brief recive data
     * 
     * @param buf allocated data buffer
     * @param buf_size size of @param buf
     * @return std::string hostname of sender
     */
    virtual std::string &recive(char *buf, uint64_t buf_size) {
        throw NotImplementedBackendMethod();
    };
};

#endif // CAPIOBACKEND_HPP
