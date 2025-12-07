#include "utils/capiocl_adapter.hpp"

/**
 * The capio_cl_engine is declared here to ensure that other components of the CAPIO server
 * can only access it through a const reference. This prevents any modifications to the engine
 * outside of those permitted by the capiocl::Engine class itself.
 */
extern capiocl::Engine *capio_cl_engine;
const capiocl::Engine &CapioCLEngine::get() { return *capio_cl_engine; }