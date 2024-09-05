#ifndef CAPIO_ENGINE_HPP
#define CAPIO_ENGINE_HPP

class CapioCLEngine {
  private:
    std::unordered_map<std::string,                         // path name
                       std::tuple<std::vector<std::string>, // Vector for producers
                                  std::vector<std::string>, // Vector for consumers
                                  std::string,              // commit rule
                                  std::string,              // fire_rule
                                  bool,                     // permanent
                                  bool,                     // exclude
                                  bool, // is_file (if true yes otherwise it is a directory)
                                  int,  // commit on file number
                                  long> // directory file count
                       >
        _locations;

    static inline std::string truncate_last_n(const std::string &str, int n) {
        return str.length() > n ? "[..] " + str.substr(str.length() - n) : str;
    }

  public:
    void print() const {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                  << "Composition of expected CAPIO FS: " << std::endl
                  << std::endl
                  << "|============================================================================"
                     "===============================================|"
                  << std::endl
                  << "|" << std::setw(124) << "|" << std::endl
                  << "|     Parsed configuration file for workflow: \e[1;36m" << workflow_name
                  << std::setw(83 - workflow_name.length()) << "\e[0m |" << std::endl
                  << "|" << std::setw(124) << "|" << std::endl
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
            std::string name_trunc = truncate_last_n(itm.first, 12);
            auto kind              = std::get<5>(itm.second) ? "F" : "D";

            std::cout << "|   " << kind << "  "
                      << "| " << name_trunc << std::setfill(' ')
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
                    auto prod1 = truncate_last_n(producers.at(i), 12);
                    std::cout << prod1 << std::setfill(' ') << std::setw(20 - prod1.length())
                              << " | ";
                } else {
                    std::cout << std::setfill(' ') << std::setw(20) << " | ";
                }

                if (i < consumers.size()) {
                    auto cons1 = truncate_last_n(consumers.at(i), 12);
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
    bool file_to_be_handled(std::filesystem::path::iterator::reference path) {
        for (const auto &entry : _locations) {
            auto capio_path = entry.first;
            if (path == capio_path) {
                return true;
            }

            if (capio_path.find('*') != std::string::npos) { // check for globs
                if (capio_path.find(path) == 0) { // if path and capio_path begins in the same way
                    return true;
                }
            }
        }

        return false;
    };

    inline void add(std::string &path, std::vector<std::string> &producers,
                    std::vector<std::string> &consumers, const std::string &commit_rule,
                    const std::string &fire_rule, bool permanent, bool exclude) {
        _locations.emplace(path, std::make_tuple(producers, consumers, commit_rule, fire_rule,
                                                 permanent, exclude, true, -1, -1));
    }

    inline void newFile(const std::string &path) {
        if (_locations.find(path) == _locations.end()) {
            _locations.emplace(
                path, std::make_tuple(std::vector<std::string>(), std::vector<std::string>(),
                                      CAPIO_FILE_COMMITTED_ON_TERMINATION, CAPIO_FILE_MODE_UPDATE,
                                      false, false, true, -1, -1));
        }
    }

    inline void addProducer(const std::string &path, std::string &producer) {
        this->newFile(path);
        producer.erase(remove_if(producer.begin(), producer.end(), isspace), producer.end());
        std::get<0>(_locations.at(path)).emplace_back(producer);
    }

    inline void addConsumer(const std::string &path, std::string &consumer) {
        this->newFile(path);
        consumer.erase(remove_if(consumer.begin(), consumer.end(), isspace), consumer.end());
        std::get<1>(_locations.at(path)).emplace_back(consumer);
    }

    inline void setCommitRule(const std::string &path, const std::string &commit_rule) {
        this->newFile(path);
        std::get<2>(_locations.at(path)) = commit_rule;
    }

    inline auto getCommitRule(const std::string &path) {
        this->newFile(path);
        return std::get<2>(_locations.at(path));
    }

    inline void setFireRule(const std::string &path, const std::string &fire_rule) {
        this->newFile(path);
        std::get<3>(_locations.at(path)) = fire_rule;
    }

    inline auto getFireRule(const std::string &path) {
        this->newFile(path);
        return std::get<3>(_locations.at(path));
    }

    inline void setPermanent(const std::string &path, bool value) {
        this->newFile(path);
        std::get<5>(_locations.at(path)) = value;
    }

    inline void setExclude(const std::string &path, bool value) {
        this->newFile(path);
        std::get<4>(_locations.at(path)) = value;
    }

    inline void setDirectory(const std::string &path) {
        this->newFile(path);
        std::get<5>(_locations.at(path)) = false;
    }

    inline bool isDirectory(const std::string &path) { return !std::get<5>(_locations.at(path)); }

    inline void setFile(const std::string &path) {
        this->newFile(path);
        std::get<5>(_locations.at(path)) = true;
    }

    inline bool isFile(const std::string &path) { return std::get<5>(_locations.at(path)); }

    inline void setCommitedNumber(const std::string &path, int num) {
        this->newFile(path);
        std::get<6>(_locations.at(path)) = num;
    }

    inline void setDirectoryFileCount(const std::string &path, long num) {
        this->newFile(path);
        std::get<7>(_locations.at(path)) = num;
    }

    inline void remove(const std::string &path) { _locations.erase(path); }

    // TODO: return vector
    inline auto producers(const std::string &path) { return std::get<0>(_locations.at(path)); }

    // TODO: return vector
    inline auto consumers(const std::string &path) { return std::get<1>(_locations.at(path)); }
};

inline CapioCLEngine *capio_cl_engine;
#endif // CAPIO_ENGINE_HPP
