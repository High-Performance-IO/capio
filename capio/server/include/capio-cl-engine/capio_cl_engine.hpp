#ifndef CAPIO_ENGINE_HPP
#define CAPIO_ENGINE_HPP
#include <filesystem>
#include <regex>
#include <unordered_map>

/**
 * @brief Engine for managing CAPIO-CL configuration entries.
 *
 * The CapioCLEngine class stores and manages configuration rules for files
 * and directories as defined in the CAPIO-CL configuration file.
 * It maintains producers, consumers, commit rules, fire rules, and other
 * metadata associated with files or directories.
 *
 * Each entry in the configuration associates a path with:
 * - Producers and consumers
 * - Commit and fire rules
 * - Flags such as permanent, excluded, directory/file type
 * - Commit-on-close counters and expected directory file counts
 * - File dependencies
 * - Regex matchers for globbing
 * - Storage policy (in-memory or on filesystem)
 */
class CapioCLEngine {
    friend class CapioFileManager;

    std::unordered_map<std::string,                         ///< Path name
                       std::tuple<std::vector<std::string>, ///< Producers list                 [0]
                                  std::vector<std::string>, ///< Consumers list                 [1]
                                  std::string,              ///< Commit rule                    [2]
                                  std::string,              ///< Fire rule                      [3]
                                  bool,                     ///< Permanent flag                 [4]
                                  bool,                     ///< Excluded flag                  [5]
                                  bool,                     ///< Is file (false = directory)    [6]
                                  int,                      ///< Commit-on-close count          [7]
                                  long,                     ///< Expected directory file count  [8]
                                  std::vector<std::string>, ///< File dependencies              [9]
                                  std::regex,               ///< Regex for glob matching        [10]
                                  bool>>                    ///< Store in FS (false = memory)   [11]
        _locations;

    /**
     * @brief Utility method to truncate a string to its last @p n characters. This is only used
     * within the print method
     *
     * If the string is longer than @p n, it prefixes the result with "[..] ".
     *
     * @param str Input string.
     * @param n Number of characters to keep from the end.
     * @return Truncated string with optional "[..]" prefix.
     */
    static std::string truncateLastN(const std::string &str, const std::size_t n) {
        return str.length() > n ? "[..] " + str.substr(str.length() - n) : str;
    }

    /**
     * Given a string, replace a single character with a string. This function is used
     * when converting a CAPIO-CL wildcard into a C++ valid regular expression
     * @param str
     * @param symbol
     * @param replacement
     * @return
     */
    [[nodiscard]] static std::string replaceSymbol(const std::string &str, char symbol,
                                                   const std::string &replacement) {
        std::string result = str;
        size_t pos         = 0;

        // Find the symbol and replace it
        while ((pos = result.find(symbol, pos)) != std::string::npos) {
            result.replace(pos, 1, replacement);
            pos += replacement.length(); // Move past the replacement
        }

        return result;
    }

    /**
     * Convert a CAPIO-CL regular expression into a c++ valid regular expression
     * @param capio_path String to convert
     * @return std::regex compiled with the corresponding c++ regex
     */
    [[maybe_unused]] [[nodiscard]] static std::regex
    generateCapioRegex(const std::string &capio_path);

  protected:
    /**
     * @brief Access the internal location map.
     *
     * @return Pointer to the internal location mapping.
     */
    const auto *getLocations() const { return &_locations; }

  public:
    /**
     * @brief Print the current CAPIO-CL configuration.
     */
    void print() const;

    /**
     * @brief Check whether a file is contained in the configuration.
     *
     * The lookup is performed by exact match or by regex globbing.
     *
     * @param file Path of the file to check.
     * @return true if the file is contained, false otherwise.
     */
    bool contains(const std::filesystem::path &file);

    /**
     * @brief Add a new CAPIO-CL configuration entry.
     *
     * @param path Path of the file or directory.
     * @param producers List of producer applications.
     * @param consumers List of consumer applications.
     * @param commit_rule Commit rule to apply.
     * @param fire_rule Fire rule to apply.
     * @param permanent Whether the file/directory is permanent.
     * @param exclude Whether the file/directory is excluded.
     * @param dependencies List of dependent files.
     */
    void add(std::string &path, std::vector<std::string> &producers,
             std::vector<std::string> &consumers, const std::string &commit_rule,
             const std::string &fire_rule, bool permanent, bool exclude,
             const std::vector<std::string> &dependencies);

    /**
     * @brief Add a new producer to a file entry.
     *
     * @param path File path.
     * @param producer Application name of the producer.
     */
    void addProducer(const std::string &path, std::string &producer);

    /**
     * @brief Add a new consumer to a file entry.
     *
     * @param path File path.
     * @param consumer Application name of the consumer.
     */
    void addConsumer(const std::string &path, std::string &consumer);

    /**
     * @brief Create a new CAPIO file entry.
     *
     * Commit and fire rules are automatically computed using the
     * longest prefix match from the configuration.
     *
     * @param path Path of the new file.
     */
    void newFile(const std::string &path);

    /**
     * @brief Remove a file from the configuration.
     *
     * @param path Path of the file to remove.
     */
    void remove(const std::string &path);

    /**
     * @brief Set the commit rule of a file.
     *
     * @param path File path.
     * @param commit_rule Commit rule string.
     */
    void setCommitRule(const std::string &path, const std::string &commit_rule);

    /**
     * @brief Set the fire rule of a file.
     *
     * @param path File path.
     * @param fire_rule Fire rule string.
     */
    void setFireRule(const std::string &path, const std::string &fire_rule);

    /**
     * @brief Mark a file as permanent or not.
     *
     * @param path File path.
     * @param value true to mark permanent, false otherwise.
     */
    void setPermanent(const std::string &path, bool value);

    /**
     * @brief Mark a file as excluded or not.
     *
     * @param path File path.
     * @param value true to exclude, false otherwise.
     */
    void setExclude(const std::string &path, bool value);

    /**
     * @brief Mark a path as a directory.
     *
     * @param path Path to mark.
     */
    void setDirectory(const std::string &path);

    /**
     * @brief Mark a path as a file.
     *
     * @param path Path to mark.
     */
    void setFile(const std::string &path);

    /**
     * @brief Set the commit-on-close counter.
     *
     * The file will be committed after @p num close operations.
     *
     * @param path File path.
     * @param num Number of close operations before commit.
     */
    void setCommitedCloseNumber(const std::string &path, int num);

    /**
     * @brief Set the expected number of files in a directory.
     *
     * @param path Directory path.
     * @param num Expected file count.
     */
    void setDirectoryFileCount(const std::string &path, long num);

    /**
     * @brief Set the dependencies of a file.
     *
     * Used for commit-on-file rules.
     *
     * @param path File path.
     * @param dependencies List of dependent files.
     */
    void setFileDeps(const std::filesystem::path &path,
                     const std::vector<std::string> &dependencies);

    /**
     * @brief Store the file in memory only.
     *
     * @param path File path.
     */
    void setStoreFileInMemory(const std::filesystem::path &path);

    /**
     * @brief Store the file on the file system.
     *
     * @param path File path.
     */
    void setStoreFileInFileSystem(const std::filesystem::path &path);

    /**
     * @brief Get the expected number of files in a directory.
     *
     * @param path Directory path.
     * @return Expected file count.
     */
    long getDirectoryFileCount(const std::string &path);

    /// @brief Get the commit rule of a file.
    std::string getCommitRule(const std::string &path);

    /// @brief Get the fire rule of a file.
    std::string getFireRule(const std::string &path);

    /// @brief Get the producers of a file.
    std::vector<std::string> getProducers(const std::string &path);

    /// @brief Get the consumers of a file.
    std::vector<std::string> getConsumers(const std::string &path);

    /// @brief Get the commit-on-close counter for a file.
    int getCommitCloseCount(std::filesystem::path::iterator::reference path) const;

    /// @brief Get file dependencies.
    std::vector<std::string> getCommitOnFileDependencies(const std::filesystem::path &path);

    /// @brief Get the list of files stored in memory.
    std::vector<std::string> getFileToStoreInMemory();

    /// @brief Get the home node of a file.
    std::string getHomeNode(const std::string &path);

    /**
     * @brief Check if a process is a producer for a file.
     *
     * TODO: The app_name parameter will be removed when client_manager is available.
     *
     * @param path File path.
     * @param pid Process ID.
     * @param app_name Application name (default "none").
     * @return true if the process is a producer, false otherwise.
     */
    bool isProducer(const std::string &path, pid_t pid, const std::string &app_name = "none");

    /**
     * @brief Check if a file is firable.
     *
     * @param path File path.
     * @return true if the file is firable, false otherwise.
     */
    bool isFirable(const std::string &path);

    /**
     * @brief Check if a path refers to a file.
     *
     * @param path File path.
     * @return true if the path is a file, false otherwise.
     */
    bool isFile(const std::string &path) const;

    /**
     * @brief Check if a path is excluded.
     *
     * @param path File path.
     * @return true if excluded, false otherwise.
     */
    bool isExcluded(const std::string &path) const;

    /**
     * @brief Check if a path is a directory.
     *
     * @param path Directory path.
     * @return true if directory, false otherwise.
     */
    bool isDirectory(const std::string &path) const;

    /**
     * @brief Check if a file is stored in memory.
     *
     * @param path File path.
     * @return true if stored in memory, false otherwise.
     */
    bool isStoredInMemory(const std::filesystem::path &path);
};

/// @brief Global pointer to the CAPIO-CL engine instance.
inline CapioCLEngine *capio_cl_engine;

#endif // CAPIO_ENGINE_HPP
