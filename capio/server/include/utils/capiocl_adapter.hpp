#ifndef CAPIO_CAPIOCL_ADAPTER_HPP
#define CAPIO_CAPIOCL_ADAPTER_HPP

#include <filesystem>
#include <string>

#include "capiocl.hpp"
#include "capiocl/engine.h"
/// @brief const wrapper to class instance of capiocl::Engine
class CapioCLEngine final {
  public:
    /// @brief Get a const reference to capiocl::Engine instance
    const static capiocl::engine::Engine &get();
};

/**
 * @brief Whether @p app_name produces @p path, or the files in it when it is a directory.
 *
 * A step creates the directories its files go in, and usually checks first whether they exist.
 * CAPIO-CL often lists only the files ("dir/*"), so the step must count as the directory's
 * producer too: it gets ENOENT instead of waiting for a directory only it will create.
 */
inline bool produces_path_or_contents(const std::filesystem::path &path,
                                      const std::string &app_name) {
    const auto &engine = CapioCLEngine::get();
    return engine.isProducer(path, app_name) ||
           engine.isProducer(path / "capio-directory-entry", app_name);
}

#endif // CAPIO_CAPIOCL_ADAPTER_HPP