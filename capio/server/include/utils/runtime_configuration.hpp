#ifndef CAPIO_RUNTIME_CONFIGURATION_HPP
#define CAPIO_RUNTIME_CONFIGURATION_HPP
#include "capiocl.hpp"
#include "capiocl/engine.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <sys/types.h>

/**
 * @brief Runtime configuration of the CAPIO server.
 *
 * This class is the only component that knows how the server settings are provided (command line
 * arguments and CAPIO_* environment variables). The rest of the server only queries the global
 * instance through RuntimeConfiguration::get(), so changes to the way the settings are provided do
 * not affect it.
 */
class RuntimeConfiguration final {
  public:
    /**
     * @brief Resolve a configuration from the command line arguments and the environment.
     * @throws std::runtime_error on invalid arguments or environment variables.
     */
    RuntimeConfiguration(int argc, char **argv);

    /**
     * @brief Parse the command line and set the global configuration. Exits after printing the
     * help, and on invalid arguments or environment variables.
     */
    static void initialize(int argc, char **argv);

    /// @brief Replace the global configuration
    static void set(RuntimeConfiguration configuration);

    /**
     * @brief Get the global configuration
     * @throws std::logic_error if called before initialize() or set()
     */
    static const RuntimeConfiguration &get();

    [[nodiscard]] bool getContinueOnError() const;

    [[nodiscard]] long getCacheLines() const;
    [[nodiscard]] long getCacheLineSize() const;
    [[nodiscard]] off64_t getFileInitialSize() const;
    [[nodiscard]] off64_t getPrefetchDataSize() const;

    [[nodiscard]] const std::string &getDiscoveryType() const;
    [[nodiscard]] const std::string &getMulticastDiscoveryAddress() const;
    [[nodiscard]] unsigned int getMulticastDiscoveryPort() const;
    [[nodiscard]] const std::string &getFsDiscoveryTokenDirectory() const;

    [[nodiscard]] const std::string &getBackendName() const;
    [[nodiscard]] const std::string &getMtclProtocol() const;
    [[nodiscard]] int getMtclPort() const;
    [[nodiscard]] int getMtclPollInterval() const;

    /// @brief How long a lookup of a file's home waits for the other servers to answer
    [[nodiscard]] std::chrono::milliseconds getLocationQueryTimeout() const;
    /// @brief How often a server waiting for a file to be created asks the other servers again
    [[nodiscard]] std::chrono::milliseconds getLocationRequeryInterval() const;

    /// @brief Whether there are CAPIO-CL rules: a config file, or the dynamic API
    [[nodiscard]] bool hasConfig() const;

    /// @brief Create the CAPIO-CL engine described by the configuration
    [[nodiscard]] capiocl::engine::Engine *createCapioClEngine() const;

    /// @brief The real directory of the virtual "/" given with -r, empty if not given
    [[nodiscard]] const std::filesystem::path &getRoot() const;

    /// @brief Where CAPIO-CL keeps the counters of on_close:N: CAPIO_METADATA_DIR, or @p root
    [[nodiscard]] std::filesystem::path getMetadataDir(const std::filesystem::path &root) const;

  private:
    /// @brief The real directory of the virtual "/" (-r), empty if not given
    std::filesystem::path root;
    /// @brief CAPIO_METADATA_DIR, empty if not set
    std::filesystem::path metadata_dir;
    bool continue_on_error = false;

    long cache_lines           = 0;
    long cache_line_size       = 0;
    off64_t file_initial_size  = 0;
    off64_t prefetch_data_size = 0;

    std::string discovery_type;
    std::string mcast_address;
    unsigned int mcast_port = 0;
    std::string token_directory;

    std::string backend_name;
    std::string mtcl_protocol;
    int mtcl_port          = 0;
    int mtcl_poll_interval = 0;

    long location_query_timeout_ms    = 0;
    long location_requery_interval_ms = 0;

    std::string capio_cl_config_path;
    bool capio_cl_dynamic_config = false;
    bool store_all_in_memory     = false;
};

#endif // CAPIO_RUNTIME_CONFIGURATION_HPP
