
#ifndef CAPIO_DISCOVERY_FS_HPP
#define CAPIO_DISCOVERY_FS_HPP

#include <ctime>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

/**
 * @brief The token files of the other servers that the filesystem discovery has read, so that each
 * scan reads only the files that are new or changed since the previous one.
 *
 * A file is changed when its inode, modification time or size is not the one cached: a server that
 * restarts on a node publishes a new token (a different address or port) under the same name, by
 * renaming a new file over the old one. The token of a file that was removed is forgotten.
 */
class FsTokenCache {
    /// @brief What identifies the version of a token file that was read, and the token it held
    struct Entry {
        ino_t inode = 0;
        timespec modified{};
        off_t size = 0;
        std::string token;
    };

    std::unordered_map<std::string, Entry> _entries;

  public:
    /**
     * @brief Look at the token files ("<host>.capio", not hidden) in @p directory.
     * @return The tokens of the files that are new, or that hold a different token than when they
     * were last read. A file that is still empty is read again at the next scan.
     */
    std::vector<std::string> scan(const std::filesystem::path &directory);

    /// @return The token last read from @p token_file, or an empty string if there is none
    [[nodiscard]] std::string token(const std::filesystem::path &token_file) const;

    /// @return The number of token files known
    [[nodiscard]] size_t size() const { return _entries.size(); }
};

/**
 * @brief Discovers CAPIO servers through token files in a shared directory.
 */
class FSDiscoveryInterface : public DiscoveryInterface {

    /// @brief Directory to look into for CAPIO tokens
    std::filesystem::path token_directory_path;
    /// @brief This server instance token filename
    std::filesystem::path token_filename;

    /// @brief Handle for file system based discovery thread
    std::thread *fs_listener_thread = nullptr;

  public:
    /**
     * @brief Construct a filesystem discovery interface.
     * @param token_directory Directory used to publish and discover token files.
     * @throws std::runtime_error If @p token_directory is empty.
     */
    explicit FSDiscoveryInterface(const std::string &token_directory);

    /// @brief Remove this server's token file and destroy the filesystem discovery interface.
    ~FSDiscoveryInterface() override;

    /**
     * @brief Publish this server's token and start scanning for other token files.
     * @param token Connection token published by this server.
     * @param adv_delay Delay in milliseconds between directory scans.
     */
    void start(const std::string &token, unsigned int adv_delay) override;

    /// @brief Stop and join the filesystem scanning thread.
    void stop() override;
};

#endif // CAPIO_DISCOVERY_FS_HPP
