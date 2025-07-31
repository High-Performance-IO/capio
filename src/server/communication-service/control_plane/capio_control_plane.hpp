#ifndef CAPIO_CONTROL_PLANE_HPP
#define CAPIO_CONTROL_PLANE_HPP

class CapioControlPlane {
  public:
    virtual ~CapioControlPlane() = default;
};

inline CapioControlPlane *capio_control_plane;

#endif // CAPIO_CONTROL_PLANE_HPP
