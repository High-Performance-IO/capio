#ifndef CAPIO_RUNTIME_CONFIGURATION_HPP
#define CAPIO_RUNTIME_CONFIGURATION_HPP
#include "capiocl/configuration.h"

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <sys/types.h>
#include <variant>
#include <vector>

/**
 * @brief Runtime configuration of the CAPIO server.
 *
 * This class is the only component that knows how the server settings are provided. Values are
 * resolved, from lowest to highest priority, from: built-in defaults, the legacy CAPIO_*
 * environment variables, the TOML configuration file and the `-o key=value` command line overrides.
 * Every option is described by a single table in runtime_configuration.cpp, which is also used to
 * validate the input and to generate the documented template printed by `capio_server --genconf`.
 *
 * The rest of the server only queries the global instance through RuntimeConfiguration::get(), so
 * changes to the way the settings are provided do not affect it.
 */
class RuntimeConfiguration final {
  public:
    /**
     * @brief Resolve a configuration.
     * @param file Optional TOML configuration file.
     * @param overrides `key=value` strings, applied after the file, in order.
     * @throws std::runtime_error on unknown keys, wrongly typed values or invalid values.
     */
    explicit RuntimeConfiguration(const std::optional<std::filesystem::path> &file = std::nullopt,
                                  const std::vector<std::string> &overrides        = {});

    /**
     * @brief Parse the command line and set the global configuration. Exits after printing the
     * help, the template or the resolved configuration, and on invalid arguments or configuration.
     */
    static void initialize(int argc, char **argv);

    /// @brief Replace the global configuration
    static void set(RuntimeConfiguration configuration);

    /**
     * @brief Get the global configuration
     * @throws std::logic_error if called before initialize() or set()
     */
    static const RuntimeConfiguration &get();

    /// @brief Write a commented TOML template listing every option with its default value
    static void writeTemplate(std::ostream &output);

    /// @brief Write the resolved values as flat TOML (`section.key = value`)
    void write(std::ostream &output) const;

    /// @brief Canonical path of the directory managed by CAPIO
    [[nodiscard]] const std::filesystem::path &getCapioDir() const;
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

    /// @brief Only the `capiocl.*` keys explicitly set by the user, forwarded verbatim to CAPIO-CL
    [[nodiscard]] const capiocl::configuration::CapioClConfiguration &
    getCapioClConfiguration() const;

  private:
    using Value = std::variant<bool, long long, std::string>;

    std::map<std::string, Value> values;
    std::filesystem::path capio_dir;
    capiocl::configuration::CapioClConfiguration capio_cl_configuration;

    template <typename T> [[nodiscard]] const T &value(const std::string &key) const;
};

#endif // CAPIO_RUNTIME_CONFIGURATION_HPP
