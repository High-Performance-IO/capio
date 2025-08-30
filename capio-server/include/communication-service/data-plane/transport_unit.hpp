#ifndef TRANSPORT_UNIT_HPP
#define TRANSPORT_UNIT_HPP

#include <string>
#include <utils/types.hpp>

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
