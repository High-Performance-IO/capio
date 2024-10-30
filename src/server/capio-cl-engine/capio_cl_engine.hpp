#ifndef CAPIO_ENGINE_HPP
#define CAPIO_ENGINE_HPP

#include "client-manager/client_manager.hpp"
#include <regex>
/**
 * @brief Class that stores the parsed configuration of the CAPIO-CL configuration file.
 *
 */
class CapioCLEngine {
  private:
    std::unordered_map<std::string,                         // path name
                       std::tuple<std::vector<std::string>, // Vector for producers          [0]
                                  std::vector<std::string>, // Vector for consumers          [1]
                                  std::string,              // commit rule                   [2]
                                  std::string,              // fire_rule                     [3]
                                  bool,                     // permanent                     [4]
                                  bool,                     // exclude                       [5]
                                  bool, // is_file (if true yes otherwise it is a directory) [6]
                                  int,  // commit on close number                            [7]
                                  long, // directory file count                              [8]
                                  std::vector<std::string>, // File dependencies             [9]
                                  std::regex, // Regex from name to match globs             [10]
                                  bool>> // Store File in memory or on FS. true = memory    [11]
        _locations;

    static std::string truncateLastN(const std::string &str, const int n) {
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
    [[nodiscard]] static std::regex generateCapioRegex(const std::string &capio_path) {
        START_LOG(gettid(), "call(capio_path=%s)", capio_path.c_str());
        auto computed = replaceSymbol(capio_path, '.', "\\.");
        computed      = CapioCLEngine::replaceSymbol(computed, '/', "\\/");
        computed      = CapioCLEngine::replaceSymbol(computed, '*', R"([a-zA-Z0-9\/\.\-_]*)");
        computed      = CapioCLEngine::replaceSymbol(computed, '+', ".");
        LOG("Computed regex: %s", computed.c_str());
        return std::regex(computed);
    }

  public:
    void print() const {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                  << "Composition of expected CAPIO FS: " << std::endl
                  << std::endl
                  << "|============================================================================"
                     "===============================================|"
                  << std::endl
                  << "|" << std::setw(124) << "|" << std::endl
                  << "|     Parsed configuration file for workflow: \e[1;36m" << workflow_name
                  << std::setw(83 - workflow_name.length()) << "\e[0m |" << std::endl
                  << "|" << std::setw(124) << "|" << std::endl
                  << "|     File color legend:     \e[48;5;034m  \e[0m File stored in memory"
                  << std::setw(72) << "|" << std::endl
                  << "|                            "
                  << "\e[48;5;172m  \e[0m File stored on file system" << std::setw(67) << "|"
                  << std::endl
                  << "|============================================================================"
                     "===============================================|"
                  << std::endl
                  << "|======|===================|===================|====================|========"
                     "============|============|===========|=========|"
                  << std::endl
                  << "| Kind | Filename          | Producer step     | Consumer step      |  "
                     "Commit Rule       |  Fire Rule | Permanent | Exclude |"
                  << std::endl
                  << "|======|===================|===================|====================|========"
                     "============|============|===========|=========|"
                  << std::endl;
        for (auto itm : _locations) {
            std::string color_preamble = std::get<11>(itm.second) ? "\e[38;5;034m" : "\e[38;5;172m";
            std::string color_post     = "\e[0m";

            std::string name_trunc = truncateLastN(itm.first, 12);
            auto kind              = std::get<6>(itm.second) ? "F" : "D";
            std::cout << "|   " << color_preamble << kind << color_post << "  | " << color_preamble
                      << name_trunc << color_post << std::setfill(' ')
                      << std::setw(20 - name_trunc.length()) << "| ";

            auto producers = std::get<0>(itm.second);
            auto consumers = std::get<1>(itm.second);
            auto rowCount =
                producers.size() > consumers.size() ? producers.size() : consumers.size();

            for (int i = 0; i <= rowCount; i++) {
                std::string prod, cons;
                if (i > 0) {
                    std::cout << "|      |                   | ";
                }

                if (i < producers.size()) {
                    auto prod1 = truncateLastN(producers.at(i), 12);
                    std::cout << prod1 << std::setfill(' ') << std::setw(20 - prod1.length())
                              << " | ";
                } else {
                    std::cout << std::setfill(' ') << std::setw(20) << " | ";
                }

                if (i < consumers.size()) {
                    auto cons1 = truncateLastN(consumers.at(i), 12);
                    std::cout << " " << cons1 << std::setfill(' ') << std::setw(20 - cons1.length())
                              << " | ";
                } else {
                    std::cout << std::setfill(' ') << std::setw(21) << " | ";
                }

                if (i == 0) {
                    std::string commit_rule = std::get<2>(itm.second),
                                fire_rule   = std::get<3>(itm.second);
                    bool exclude = std::get<4>(itm.second), permanent = std::get<5>(itm.second);

                    std::cout << " " << commit_rule << std::setfill(' ')
                              << std::setw(20 - commit_rule.length()) << " | " << fire_rule
                              << std::setfill(' ') << std::setw(13 - fire_rule.length()) << " | "
                              << "    " << (permanent ? "YES" : "NO ") << "   |   "
                              << (exclude ? "YES" : "NO ") << "   |" << std::endl;
                } else {
                    std::cout << std::setfill(' ') << std::setw(20) << "|" << std::setfill(' ')
                              << std::setw(13) << "|" << std::setfill(' ') << std::setw(12) << "|"
                              << std::setfill(' ') << std::setw(10) << "|" << std::endl;
                }
            }
            std::cout << "*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
                         "~~~~~~~"
                         "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*"
                      << std::endl;
        }
        std::cout << std::endl;
    };

    // TODO: might need to be improved
    static bool fileToBeHandled(std::filesystem::path::iterator::reference path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());

        if (path == get_capio_dir()) {
            LOG("Path is capio_dir. Ignoring.");
            return false;
        }

        LOG("Parent path=%s", path.parent_path().c_str());
        LOG("Path %s be handled by CAPIO",
            path.parent_path().string().rfind(get_capio_dir(), 0) == 0 ? "SHOULD" : "SHOULD NOT");
        return path.parent_path().string().rfind(get_capio_dir(), 0) == 0;
    };

    /**
     * Check whether the file is contained inside the location, either by direct name or by glob
     * @param file
     * @return
     */
    bool contains(const std::filesystem::path &file) {
        return std::any_of(_locations.begin(), _locations.end(), [&](auto &itm) {
            return std::regex_match(file.c_str(), std::get<10>(itm.second));
        });
    }

    void add(std::string &path, std::vector<std::string> &producers,
             std::vector<std::string> &consumers, const std::string &commit_rule,
             const std::string &fire_rule, bool permanent, bool exclude,
             const std::vector<std::string> &dependencies) {
        START_LOG(gettid(), "call(path=%s, commit=%s, fire=%s, permanent=%s, exclude=%s)",
                  path.c_str(), commit_rule.c_str(), fire_rule.c_str(), permanent ? "YES" : "NO",
                  exclude ? "YES" : "NO");

        _locations.emplace(path, std::make_tuple(producers, consumers, commit_rule, fire_rule,
                                                 permanent, exclude, true, -1, -1, dependencies,
                                                 CapioCLEngine::generateCapioRegex(path), false));
    }

    void newFile(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (_locations.find(path) == _locations.end()) {
            std::string commit = CAPIO_FILE_COMMITTED_ON_TERMINATION;
            std::string fire   = CAPIO_FILE_MODE_UPDATE;

            /*
             * Inherit commit and fire rules from LPM directory
             * matchSize is used to compute LPM
             */
            size_t matchSize = 0;
            for (const auto &[filename, data] : _locations) {
                if (std::regex_match(path, std::get<10>(data)) && filename.length() > matchSize) {
                    LOG("Found match with %s", filename.c_str());
                    matchSize = filename.length();
                    commit    = std::get<2>(data);
                    fire      = std::get<3>(data);
                }
            }
            LOG("Adding file %s to _locations with commit=%s, and fire=%s", path.c_str(),
                commit.c_str(), fire.c_str());
            _locations.emplace(path,
                               std::make_tuple(std::vector<std::string>(),
                                               std::vector<std::string>(), commit, fire, false,
                                               false, true, -1, -1, std::vector<std::string>(),
                                               CapioCLEngine::generateCapioRegex(path), false));
        }
    }

    long getDirectoryFileCount(const std::string &path) {
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<8>(itm->second);
        }
        return 0;
    }

    void addProducer(const std::string &path, std::string &producer) {
        START_LOG(gettid(), "call(path=%s, producer=%s)", path.c_str(), producer.c_str());
        producer.erase(remove_if(producer.begin(), producer.end(), isspace), producer.end());
        newFile(path);
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<0>(itm->second).emplace_back(producer);
        }
    }

    void addConsumer(const std::string &path, std::string &consumer) {
        START_LOG(gettid(), "call(path=%s, consumer=%s)", path.c_str(), consumer.c_str());
        consumer.erase(remove_if(consumer.begin(), consumer.end(), isspace), consumer.end());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<1>(itm->second).emplace_back(consumer);
        }
    }

    void setCommitRule(const std::string &path, const std::string &commit_rule) {
        START_LOG(gettid(), "call(path=%s, commit_rule=%s)", path.c_str(), commit_rule.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<2>(itm->second) = commit_rule;
        }
    }

    std::string getCommitRule(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            LOG("Commit rule: %s", std::get<2>(_locations.at(path)).c_str());
            return std::get<2>(itm->second);
        }

        /*
         * For caching purpose, each new file is then added to the map if not found,
         * with its data being instantiated from the metadata of the most likely matched glob
         * TODO: check overhead of this
         */
        LOG("No entry found on map. checking globs. Creating new file from globs");
        this->newFile((path));
        LOG("Returning DEFAULT Fire rule for file %s (update)", path.c_str());
        return std::get<2>(_locations.at((path)));
    }

    void setFireRule(const std::string &path, const std::string &fire_rule) {
        START_LOG(gettid(), "call(path=%s, fire_rule=%s)", path.c_str(), fire_rule.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<3>(itm->second) = fire_rule;
        }
    }

    bool isFirable(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            LOG("Fire rule for file %s is %s", path.c_str(), std::get<3>(itm->second).c_str());
            return std::get<3>(itm->second) == CAPIO_FILE_MODE_NO_UPDATE;
        }
        /*
         * For caching purpose, each new file is then added to the map if not found,
         * with its data being instantiated from the metadata of the most likely matched glob
         * TODO: check overhead of this
         */
        LOG("No entry found on map. checking globs. Creating new file from globs");
        this->newFile((path));
        LOG("Fire rule for file %s is  %s", path.c_str(),
            std::get<3>(_locations.at((path))).c_str());
        return std::get<3>(_locations.at((path))) == CAPIO_FILE_MODE_NO_UPDATE;
    }

    void setPermanent(const std::string &path, bool value) {
        START_LOG(gettid(), "call(path=%s, value=%s)", path.c_str(), value ? "true" : "false");
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<4>(itm->second) = value;
        }
    }

    void setExclude(const std::string &path, const bool value) {
        START_LOG(gettid(), "call(path=%s, value=%s)", path.c_str(), value ? "true" : "false");
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<5>(itm->second) = value;
        }
    }

    void setDirectory(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (_locations.find(path) != _locations.end()) {
            std::get<6>(_locations.at(path)) = false;
        }
    }

    void setFile(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<6>(itm->second) = true;
        }
    }

    bool isFile(const std::string &path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<6>(itm->second);
        }
        return false;
    }

    bool isDirectory(const std::string &path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        return !isFile(path);
    }

    void setCommitedNumber(const std::string &path, const int num) {
        START_LOG(gettid(), "call(path=%s, num=%ld)", path.c_str(), num);
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<7>(itm->second) = num;
        }
    }

    void setDirectoryFileCount(const std::string &path, long num) {
        START_LOG(gettid(), "call(path=%s, num=%ld)", path.c_str(), num);
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<8>(itm->second) = num;
        }
    }

    void remove(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        _locations.erase(path);
    }

    // TODO: return vector
    std::vector<std::string> producers(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<0>(itm->second);
        }
        return {};
    }

    // TODO: return vector
    std::vector<std::string> consumers(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<1>(itm->second);
        }
        return {};
    }

    bool isProducer(const std::string &path, const pid_t pid) {
        START_LOG(gettid(), "call(path=%s, pid=%ld", path.c_str(), pid);

        const auto app_name = client_manager->get_app_name(pid);
        LOG("App name for tid %d is %s", pid, app_name.c_str());

        // check for exact entry
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            LOG("Found exact match for path");
            std::vector<std::string> producers = std::get<0>(itm->second);
            DBG(gettid(), [&](const std::vector<std::string> &arr) {
                for (auto elem : arr) {
                    LOG("producer: %s", elem.c_str());
                }
            }(producers));
            return std::find(producers.begin(), producers.end(), app_name) != producers.end();
        }
        LOG("No exact match found in locations. checking for globs");
        // check for glob
        for (const auto &[k, entry] : _locations) {
            if (std::regex_match(path, std::get<10>(entry))) {
                LOG("Found possible glob match");
                std::vector<std::string> producers = std::get<0>(entry);
                DBG(gettid(), [&](const std::vector<std::string> &arr) {
                    for (auto itm : arr) {
                        LOG("producer: %s", itm.c_str());
                    }
                }(producers));
                return std::find(producers.begin(), producers.end(), app_name) != producers.end();
            }
        }
        LOG("No match has been found");
        return false;
    }

    void setFileDeps(const std::filesystem::path &path,
                     const std::vector<std::string> &dependencies) {
        START_LOG(gettid(), "call()");
        std::get<9>(_locations.at(path)) = dependencies;
        for (const auto &itm : dependencies) {
            LOG("Creating new fie (if it exists) for path %s", itm.c_str());
            newFile(itm);
        }
    }

    int getCommitCloseCount(std::filesystem::path::iterator::reference path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        int count = 0;
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            count = std::get<7>(itm->second);
        }
        LOG("Expected number on close to commit file: %d", count);
        return count;
    };

    // todo fix leak
    std::vector<std::string> get_file_deps(const std::filesystem::path &path) {
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<9>(itm->second);
        }
        return {};
    }

    void setStoreFileInMemory(const std::filesystem::path &path) {
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<11>(itm->second) = true;
        }
    }

    void setStoreFileInFileSystem(const std::filesystem::path &path) {
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            std::get<11>(itm->second) = false;
        }
    }

    bool storeFileInMemory(const std::filesystem::path &path) {
        if (const auto itm = _locations.find(path); itm != _locations.end()) {
            return std::get<11>(itm->second);
        }
        return false;
    }

    std::vector<std::string> getFileToStoreInMemory() {
        START_LOG(gettid(), "call()");
        std::vector<std::string> files;

        for (const auto &[path, file] : _locations) {
            if (std::get<11>(file)) {
                files.push_back(path);
            }
        }

        return files;
    }
};

inline CapioCLEngine *capio_cl_engine;
#endif // CAPIO_ENGINE_HPP
