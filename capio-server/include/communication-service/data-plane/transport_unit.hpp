#ifndef TRANSPORT_UNIT_HPP
#define TRANSPORT_UNIT_HPP

#include <include/utils/types.hpp>
#include <string>

class TransportUnit {
  protected:
    std::string _filepath;
    char *_bytes{};
    capio_off64_t _buffer_size{};
    capio_off64_t _start_write_offset{};

  public:
    TransportUnit() = default;

    ~TransportUnit() { delete[] _bytes; }

    friend class MTCLBackend;
};

#endif // TRANSPORT_UNIT_HPP
