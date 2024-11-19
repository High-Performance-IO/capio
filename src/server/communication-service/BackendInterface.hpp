#ifndef CAPIOBACKEND_HPP
#define CAPIOBACKEND_HPP

class BackendInterface
{
public:
    /**
     * @brief Send data to target
     *
     * @param target Hostname of remote target
     * @param buf pointer to data to sent
     * @param buf_size length of @param buf
     */
    virtual void send(const std::string &target, char *buf, uint64_t buf_size) = 0;

    /**
     * @brief recive data
     * 
     * @param buf allocated data buffer
     * @param buf_size size of @param buf
     * @return std::string hostname of sender
     */
    virtual std::string &recive(char *buf, uint64_t buf_size) = 0;
};

#endif // CAPIOBACKEND_HPP
