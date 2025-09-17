#ifndef CAPIO_ENGINE_HPP
#define CAPIO_ENGINE_HPP

#include <include/client-manager/client_manager.hpp>
#include <regex>
#include <unordered_map>

#include "capio/filesystem.hpp"
/**
 * @brief Class that stores the parsed configuration of the CAPIO-CL configuration file.
 *
 */

class CapioCLEngine {
    friend class CapioFileManager;
    std::unordered_map<std::string,                         // path name
                       std::tuple<std::vector<std::string>, // Vector for producers            [0]
                                  std::vector<std::string>, // Vector for consumers            [1]
                                  std::string,              // commit rule                     [2]
                                  std::string,              // fire_rule                       [3]
                                  bool,                     // permanent                       [4]
                                  bool,                     // exclude                         [5]
                                  bool,                     // is_file (false = directory)     [6]
                                  int,                      // commit on close number          [7]
                                  long,                     // directory file count            [8]
                                  std::vector<std::string>, // File dependencies               [9]
                                  std::regex,               // Regex to match globs            [10]
                                  bool>>                    // Store File on FS. true = memory [11]
        _locations;

    static std::string truncateLastN(const std::string &str, const std::size_t n) {
        return str.length() > n ? "[..] " + str.substr(str.length() - n) : str;
    }

  protected:
    const auto *getLocations() const { return &_locations; }

  public:
    void print() const;

    /**
     * Check whether the file is contained inside the location, either by direct name or by glob
     * @param file
     * @return
     */
    bool contains(const std::filesystem::path &file);

    /**
     * Check whether the file is contained inside the location, either by direct name or by glob
     * @param file
     * @return
     */

    void add(std::string &path, std::vector<std::string> &producers,
             std::vector<std::string> &consumers, const std::string &commit_rule,
             const std::string &fire_rule, bool permanent, bool exclude,
             const std::vector<std::string> &dependencies);

    void newFile(const std::string &path);
    long getDirectoryFileCount(const std::string &path);
    void addProducer(const std::string &path, std::string &producer);
    void addConsumer(const std::string &path, std::string &consumer);
    void setCommitRule(const std::string &path, const std::string &commit_rule);
    std::string getCommitRule(const std::string &path);
    void setFireRule(const std::string &path, const std::string &fire_rule);
    bool isFirable(const std::string &path);
    void setPermanent(const std::string &path, bool value);
    void setExclude(const std::string &path, bool value);
    void setDirectory(const std::string &path);
    void setFile(const std::string &path);
    bool isFile(const std::string &path) const;
    bool isDirectory(const std::string &path) const;
    void setCommitedNumber(const std::string &path, int num);
    void setDirectoryFileCount(const std::string &path, long num);
    void remove(const std::string &path);
    std::vector<std::string> producers(const std::string &path);
    std::vector<std::string> consumers(const std::string &path);
    bool isProducer(const std::string &path, pid_t pid);
    void setFileDeps(const std::filesystem::path &path,
                     const std::vector<std::string> &dependencies);
    int getCommitCloseCount(std::filesystem::path::iterator::reference path) const;
    std::vector<std::string> get_file_deps(const std::filesystem::path &path);
    void setStoreFileInMemory(const std::filesystem::path &path);
    void setStoreFileInFileSystem(const std::filesystem::path &path);
    bool storeFileInMemory(const std::filesystem::path &path);
    std::vector<std::string> getFileToStoreInMemory();
    std::string get_home_node(const std::string &path);
};

inline CapioCLEngine *capio_cl_engine;

#endif // CAPIO_ENGINE_HPP
