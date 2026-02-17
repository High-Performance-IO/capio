#ifndef CAPIO_CAPIOCL_ADAPTER_HPP
#define CAPIO_CAPIOCL_ADAPTER_HPP

#include "capiocl.hpp"
#include "capiocl/engine.h"
/// @brief const wrapper to class instance of capiocl::Engine
class CapioCLEngine final {
  public:
    /// @brief Get a const reference to capiocl::Engine instance
    const static capiocl::engine::Engine &get();
};

#endif // CAPIO_CAPIOCL_ADAPTER_HPP