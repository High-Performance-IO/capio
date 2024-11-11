#ifndef CAPIOBACKEND_HPP
#define CAPIOBACKEND_HPP

class CapioBackend {
  protected:
  public:
    virtual ~CapioBackend()  = default;
    virtual void handshake() = 0;
    virtual void send()      = 0;
    virtual void receive()   = 0;
};

#endif // CAPIOBACKEND_HPP
