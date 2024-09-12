#ifndef CAPIO_ENGINE_HPP
#define CAPIO_ENGINE_HPP

#include "client-manager/client_manager.hpp"
#include "utils/common.hpp"

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
                                  int,  // commit on file number                             [7]
                                  long> // directory file count                              [8]
                       >
        _locations;

    static std::string truncate_last_n(const std::string &str, int n) {
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
            auto kind              = std::get<6>(itm.second) ? "F" : "D";
            std::cout << "|   " << kind << "  " << "| " << name_trunc << std::setfill(' ')
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
    bool file_to_be_handled(std::filesystem::path::iterator::reference path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
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

    void add(std::string &path, std::vector<std::string> &producers,
             std::vector<std::string> &consumers, const std::string &commit_rule,
             const std::string &fire_rule, bool permanent, bool exclude) {
        START_LOG(gettid(), "call(path=%s, commit=%s, fire=%s, permanent=%s, exclude=%s)",
                  path.c_str(), commit_rule.c_str(), fire_rule.c_str(), permanent ? "YES" : "NO",
                  exclude ? "YES" : "NO");
        _locations.emplace(path, std::make_tuple(producers, consumers, commit_rule, fire_rule,
                                                 permanent, exclude, true, -1, -1));
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
                if (match_globs(filename, path) && this->isDirectory(filename) &&
                    filename.length() > matchSize) {
                    matchSize = filename.length();
                    commit    = this->getCommitRule(filename);
                    fire      = this->getFireRule(filename);
                }
            }

            _locations.emplace(path, std::make_tuple(std::vector<std::string>(),
                                                     std::vector<std::string>(), commit, fire,
                                                     false, false, true, -1, -1));
        }
    }

    long getDirectoryFileCount(std::string path) {
        if (_locations.find(path) != _locations.end()) {
            return std::get<8>(_locations.at(path));
        }
        return 0;
    }

    void addProducer(const std::string &path, std::string &producer) {
        START_LOG(gettid(), "call(path=%s, producer=%s)", path.c_str(), producer.c_str());
        producer.erase(remove_if(producer.begin(), producer.end(), isspace), producer.end());
        std::get<0>(_locations.at(path)).emplace_back(producer);
    }

    void addConsumer(const std::string &path, std::string &consumer) {
        START_LOG(gettid(), "call(path=%s, consumer=%s)", path.c_str(), consumer.c_str());
        consumer.erase(remove_if(consumer.begin(), consumer.end(), isspace), consumer.end());
        std::get<1>(_locations.at(path)).emplace_back(consumer);
    }

    void setCommitRule(const std::string &path, const std::string &commit_rule) {
        START_LOG(gettid(), "call(path=%s, commit_rule=%s)", path.c_str(), commit_rule.c_str());
        std::get<2>(_locations.at(path)) = commit_rule;
    }

    std::string getCommitRule(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        LOG("Fire rule: %s", std::get<2>(_locations.at(path)).c_str());
        return std::get<2>(_locations.at(path));
    }

    void setFireRule(const std::string &path, const std::string &fire_rule) {
        START_LOG(gettid(), "call(path=%s, fire_rule=%s)", path.c_str(), fire_rule.c_str());
        std::get<3>(_locations.at(path)) = fire_rule;
    }

    std::string getFireRule(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        if(_locations.find(path) == _locations.end()) {
            return CAPIO_FILE_MODE_UPDATE;
        }
        return std::get<3>(_locations.at(path));
    }

    void setPermanent(const std::string &path, bool value) {
        START_LOG(gettid(), "call(path=%s, value=%s)", path.c_str(), value ? "true" : "false");
        std::get<4>(_locations.at(path)) = value;
    }

    void setExclude(const std::string &path, const bool value) {
        START_LOG(gettid(), "call(path=%s, value=%s)", path.c_str(), value ? "true" : "false");
        std::get<5>(_locations.at(path)) = value;
    }

    void setDirectory(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        std::get<6>(_locations.at(path)) = false;
    }

    bool isDirectory(const std::string &path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        return !std::get<6>(_locations.at(path));
    }

    void setFile(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        std::get<6>(_locations.at(path)) = true;
    }

    bool isFile(const std::string &path) const {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        return std::get<6>(_locations.at(path));
    }

    void setCommitedNumber(const std::string &path, const int num) {
        START_LOG(gettid(), "call(path=%s, num=%ld)", path.c_str(), num);
        std::get<7>(_locations.at(path)) = num;
    }

    void setDirectoryFileCount(const std::string &path, long num) {
        START_LOG(gettid(), "call(path=%s, num=%ld)", path.c_str(), num);
        std::get<8>(_locations.at(path)) = num;
    }

    void remove(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        _locations.erase(path);
    }

    // TODO: return vector
    auto producers(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        return std::get<0>(_locations.at(path));
    }

    // TODO: return vector
    auto consumers(const std::string &path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        return std::get<1>(_locations.at(path));
    }

    bool isProducer(const std::string &path, const pid_t pid) {
        START_LOG(gettid(), "call(path=%s, pid=%ld", path.c_str(), pid);

        const auto app_name = client_manager->get_app_name(pid);
        LOG("App name for tid %d is %s", pid, app_name.c_str());

        // check for exact entry
        if (_locations.find(path) != _locations.end()) {
            LOG("Found exact match for path");
            std::vector<std::string> producers = std::get<0>(_locations.at(path));
            return std::find(producers.begin(), producers.end(), app_name) != producers.end();
        }
        LOG("No exact match found in locations. checking for globs");
        // check for glob
        for (const auto &[k, entry] : _locations) {
            if (match_globs(k, path)) {
                LOG("Found possible glob match");
                std::vector<std::string> producers = std::get<0>(_locations.at(k));
                return std::find(producers.begin(), producers.end(), app_name) != producers.end();
            }
        }
        LOG("No match has been found");
        return false;
    }
};

CapioCLEngine *capio_cl_engine;
#endif // CAPIO_ENGINE_HPP
