#ifndef CAPIO_SERVER_UTILS_FS_ROOT_HPP
#define CAPIO_SERVER_UTILS_FS_ROOT_HPP

#include <atomic>
#include <filesystem>

/**
 * @brief The real directory of the virtual "/" that the paths of the clients are relative to.
 *
 * Set once: from -r at startup, or else from the CAPIO_DIR of the first file system streaming
 * client. It never changes afterwards, so it is read without locks. The server uses the one of
 * fs_root().
 */
class FsRoot {
    std::filesystem::path _root;
    std::atomic<bool> _set = false;

  public:
    /// @brief Set the root, if it is not set yet. Called by one thread: the server's main loop.
    /// @return whether it was set by this call
    bool set(const std::filesystem::path &root) {
        if (_set.load(std::memory_order_acquire)) {
            return false;
        }
        _root = root;
        _set.store(true, std::memory_order_release);
        return true;
    }

    /// @brief The root, empty if it is not known yet
    [[nodiscard]] std::filesystem::path get() const {
        return _set.load(std::memory_order_acquire) ? _root : std::filesystem::path{};
    }

    /// @brief Whether the root is known
    [[nodiscard]] bool known() const { return _set.load(std::memory_order_acquire); }

    /// @brief Where the virtual @p path is on the file system: under the root, or @p path itself
    /// when the root is not known
    [[nodiscard]] std::filesystem::path real(const std::filesystem::path &path) const {
        return known() ? _root / path.relative_path() : path;
    }

    /// @brief The virtual path of @p real_path, a path under the root: the inverse of real()
    [[nodiscard]] std::filesystem::path virtualPath(const std::filesystem::path &real_path) const {
        if (!known()) {
            return real_path;
        }
        const auto relative = real_path.lexically_relative(_root);
        return relative == "." ? std::filesystem::path("/") : "/" / relative;
    }
};

/// @brief The root of this server
inline FsRoot &fs_root() {
    static FsRoot root;
    return root;
}

#endif // CAPIO_SERVER_UTILS_FS_ROOT_HPP
