
#ifndef CAPIO_DISCOVERY_FS_HPP
#define CAPIO_DISCOVERY_FS_HPP

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
