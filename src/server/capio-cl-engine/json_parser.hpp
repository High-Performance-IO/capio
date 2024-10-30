#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP
#include <singleheader/simdjson.h>

/**
 * @brief Contains the code to parse a JSON based CAPIO-CL configuration file
 *
 */
class JsonParser {

    /**
     * @brief Check if a string is a representation of a integer number
     *
     * @param s
     * @return true
     * @return false
     */
    static inline bool is_int(const std::string &s) {
        START_LOG(gettid(), "call(%s)", s.c_str());
        bool res = false;
        if (!s.empty()) {
            char *p;
            strtol(s.c_str(), &p, 10);
            res = *p == 0;
        }
        return res;
    }

    /**
     * @brief compare two paths
     *
     * @param path
     * @param base
     * @return true if path is a subdirectory of base
     * @return false otherwise
     */
    static inline bool first_is_subpath_of_second(const std::filesystem::path &path,
                                                  const std::filesystem::path &base) {
        const auto mismatch_pair =
            std::mismatch(path.begin(), path.end(), base.begin(), base.end());
        return mismatch_pair.second == base.end();
    }

  public:
    /**
     * @brief Perform the parsing of the capio_server configuration file
     *
     * @param source
     * @return CapioCLEngine instance with the information provided by the config file
     */
    static CapioCLEngine *parse(const std::filesystem::path &source) {
        auto locations        = new CapioCLEngine();
        const auto &capio_dir = get_capio_dir();

        START_LOG(gettid(), "call(config_file='%s', capio_dir='%s')", source.c_str(),
                  capio_dir.c_str());

        locations->newFile(get_capio_dir());
        if (source.empty()) {
            return locations;
        }

        simdjson::ondemand::parser parser;
        simdjson::padded_string json;
        simdjson::ondemand::document entries;
        simdjson::ondemand::array input_stream, output_stream, streaming, permanent_files,
            exclude_files, storage_memory, storage_fs;
        simdjson::ondemand::object storage_section;
        simdjson::error_code error;

        try {
            json = simdjson::padded_string::load(source.c_str());
        } catch (const simdjson::simdjson_error &e) {
            std::cerr << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                      << "Exception thrown while opening config file: " << e.what() << std::endl;
            LOG("Exception thrown while opening config file: %s", e.what());
            ERR_EXIT("Exception thrown while opening config file: %s", e.what());
        }

        entries = parser.iterate(json);
        std::string_view wf_name;
        error = entries["name"].get_string().get(wf_name);
        if (error) {
            ERR_EXIT("Error: workflow name is mandatory");
        }
        workflow_name = std::string(wf_name);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                  << "Parsing configuration for workflow: " << workflow_name << std::endl;
        LOG("Parsing configuration for workflow: %s", std::string(workflow_name).c_str());

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] " << std::endl;

        auto io_graph = entries["IO_Graph"];

        for (auto app : io_graph) {
            std::string_view app_name;
            error = app["name"].get_string().get(app_name);
            if (error) {
                ERR_EXIT("Error: app name is mandatory");
            }

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                      << "Parsing config for app " << app_name << std::endl;
            LOG("Parsing config for app %s", std::string(app_name).c_str());

            if (app["input_stream"].get_array().get(input_stream)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                          << "No input_stream section found for app " << app_name << std::endl;
                ERR_EXIT("No input_stream section found for app %s", std::string(app_name).c_str());
            } else {
                for (auto itm : input_stream) {
                    std::filesystem::path file(itm.get_string().take_value());
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                              << "Found file : " << file << std::endl;
                    if (file.is_relative() || first_is_subpath_of_second(file, get_capio_dir())) {
                        std::string appname(app_name);
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                                  << "File : " << file << " added to app: " << app_name
                                  << std::endl;
                        if (file.is_relative()) {
                            file = capio_dir / file;
                        }
                        locations->newFile(file.c_str());
                        locations->addConsumer(file, appname);
                    } else {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name
                                  << " ] "
                                  << "File : " << file
                                  << " is not relative to CAPIO_DIR. Ignoring..." << std::endl;
                    }
                }

                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                          << "Completed input_stream parsing for app: " << app_name << std::endl;
                LOG("Completed input_stream parsing for app: %s", std::string(app_name).c_str());
            }

            if (app["output_stream"].get_array().get(output_stream)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                          << "No output_stream section found for app " << app_name << std::endl;
                ERR_EXIT("No output_stream section found for app %s",
                         std::string(app_name).c_str());
            } else {
                for (auto itm : output_stream) {
                    std::filesystem::path file(itm.get_string().take_value());
                    if (file.is_relative() || first_is_subpath_of_second(file, get_capio_dir())) {
                        std::string appname(app_name);
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                                  << "Adding file: " << file << " to app: " << app_name
                                  << std::endl;
                        if (file.is_relative()) {
                            file = capio_dir / file;
                        }
                        locations->newFile(file);
                        locations->addProducer(file, appname);
                    }
                }
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                          << "Completed output_stream parsing for app: " << app_name << std::endl;
                LOG("Completed output_stream parsing for app: %s", std::string(app_name).c_str());
            }

            // PARSING STREAMING FILES
            if (app["streaming"].get_array().get(streaming)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                          << "No streaming section found for app: " << app_name << std::endl;
                LOG("No streaming section found for app: %s", std::string(app_name).c_str());
            } else {
                LOG("Began parsing streaming section for app %s", std::string(app_name).c_str());
                for (auto file : streaming) {
                    std::string_view committed, mode, commit_rule;
                    std::vector<std::filesystem::path> streaming_names;
                    std::vector<std::string> file_deps;
                    long int n_close = -1;
                    long n_files, batch_size;
                    bool is_file = true;

                    simdjson::ondemand::array name;
                    error = file["name"].get_array().get(name);
                    if (error || name.is_empty()) {
                        error = file["dirname"].get_array().get(name);
                        if (error || name.is_empty()) {
                            std::cout
                                << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                                << "error: either name or dirname in streaming section is required"
                                << std::endl;
                            ERR_EXIT(
                                "error: either name or dirname in streaming section is required");
                        }
                        is_file = false;
                    }

                    for (auto item : name) {
                        std::string_view elem = item.get_string().value();
                        LOG("Found name: %s", std::string(elem).c_str());
                        std::filesystem::path file_fs(elem);
                        if (file_fs.is_relative() ||
                            first_is_subpath_of_second(file_fs, get_capio_dir())) {
                            LOG("Saving file %s to locations", std::string(elem).c_str());
                            streaming_names.emplace_back(elem);
                        }
                    }

                    // PARSING COMMITTED
                    error = file["committed"].get_string().get(committed);
                    if (error) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                                  << "commit rule is mandatory in streaming section" << std::endl;
                        ERR_EXIT("error commit rule is mandatory in streaming section");
                    } else {
                        auto pos = committed.find(':');
                        if (pos != std::string::npos) {
                            commit_rule = committed.substr(0, pos);
                            if (commit_rule != CAPIO_FILE_COMMITTED_ON_CLOSE) {
                                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name
                                          << " ] "
                                          << "commit rule " << commit_rule << std::endl;
                                ERR_EXIT("error commit rule: %s", std::string(commit_rule).c_str());
                            }

                            std::string n_close_str(committed.substr(pos + 1, committed.length()));

                            if (!is_int(n_close_str)) {
                                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name
                                          << " ] "
                                          << "commit rule on_close invalid number" << std::endl;
                                ERR_EXIT("error commit rule on_close invalid number: !is_int()");
                            }
                            n_close = std::stol(n_close_str);
                        } else {
                            commit_rule = committed;
                        }
                    }

                    // check for committed on file:
                    if (commit_rule == CAPIO_FILE_COMMITTED_ON_FILE) {
                        simdjson::ondemand::array file_deps_tmp;
                        error = file["file_deps"].get_array().get(file_deps_tmp);

                        if (error) {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name
                                      << " ] "
                                      << "commit rule is on_file but no file_deps section found"
                                      << std::endl;
                            ERR_EXIT("commit rule is on_file but no file_deps section found");
                        }

                        std::string_view name_tmp;
                        for (auto itm : file_deps_tmp) {
                            name_tmp = itm.get_string().value();
                            std::filesystem::path computed_path(name_tmp);
                            computed_path = computed_path.is_relative()
                                                ? (get_capio_dir() / computed_path)
                                                : computed_path;
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name
                                      << " ] "
                                      << "Adding file: " << computed_path
                                      << " to file dependencies: " << std::endl;
                            file_deps.emplace_back(computed_path);
                        }
                    }

                    LOG("Committed: %s", std::string(committed).c_str());
                    // END PARSING COMMITTED

                    error = file["mode"].get_string().get(mode);
                    if (error) {
                        mode = CAPIO_FILE_MODE_UPDATE;
                    }
                    LOG("Mode: %s", std::string(mode).c_str());

                    error = file["n_files"].get_int64().get(n_files);
                    if (error) {
                        n_files = -1;
                    }
                    LOG("n_files: %d", n_files);

                    error = file["batch_size"].get_int64().get(batch_size);
                    if (error) {
                        batch_size = 0;
                    }
                    LOG("batch_size: %d", batch_size);
                    for (auto path : streaming_names) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                                  << " [ " << node_name << " ] "
                                  << "Updating metadata for path:  " << path << std::endl;
                        if (path.is_relative()) {
                            path = (capio_dir / path).lexically_normal();
                        }
                        LOG("path: %s", path.c_str());

                        // TODO: check for globs
                        std::string commit(commit_rule), firerule(mode);
                        if (n_files != -1) {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name
                                      << " ] "
                                      << "Setting path:  " << path << " n_files to " << n_files
                                      << std::endl;
                            locations->setDirectoryFileCount(path, n_files);
                        }

                        is_file ? locations->setFile(path) : locations->setDirectory(path);
                        locations->setCommitRule(path, commit);
                        locations->setFireRule(path, firerule);
                        locations->setCommitedNumber(path, n_close);
                        locations->setFileDeps(path, file_deps);
                    }
                }

                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                          << "completed parsing of streaming section for app: " << app_name
                          << std::endl;
                LOG("completed parsing of streaming section for app: %s",
                    std::string(app_name).c_str());
            } // END PARSING STREAMING FILES
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                      << std::endl;
        } // END OF APP MAIN LOOPS
        LOG("Completed parsing of io_graph app main loops");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                  << "Completed parsing of io_graph" << std::endl;
        LOG("Completed parsing of io_graph");

        if (entries["permanent"].get_array().get(permanent_files)) { // PARSING PERMANENT FILES
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                      << "No permanent section found for workflow: " << workflow_name << std::endl;
            LOG("No permanent section found for workflow: %s", std::string(workflow_name).c_str());
        } else {
            for (auto file : permanent_files) {
                std::string_view name;
                error = file.get_string().get(name);
                if (error) {
                    ERR_EXIT("error name for permanent section is mandatory");
                }
                LOG("Permanent name: %s", std::string(name).c_str());

                std::filesystem::path path(name);

                if (path.is_relative()) {
                    path = (capio_dir / path).lexically_normal();
                }
                // TODO: check for globs
                if (first_is_subpath_of_second(path, get_capio_dir())) {
                    locations->setPermanent(name.data(), true);
                }
            }
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                      << "Completed parsing of permanent files" << std::endl;
            LOG("Completed parsing of permanent files");
        } // END PARSING PERMANENT FILES

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] " << std::endl;

        if (entries["exclude"].get_array().get(exclude_files)) { // PARSING PERMANENT FILES
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                      << "No exclude section found for workflow: " << workflow_name << std::endl;
            LOG("No exclude section found for workflow: %s", std::string(workflow_name).c_str());
        } else {
            for (auto file : exclude_files) {
                std::string_view name;
                error = file.get_string().get(name);
                if (error) {
                    ERR_EXIT("error name for exclude section is mandatory");
                }
                LOG("exclude name: %s", std::string(name).c_str());

                std::filesystem::path path(name);

                if (path.is_relative()) {
                    path = (capio_dir / path).lexically_normal();
                }
                // TODO: check for globs
                if (first_is_subpath_of_second(path, get_capio_dir())) {
                    locations->setExclude(name.data(), true);
                }
            }
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                      << "Completed parsing of exclude files" << std::endl;
            LOG("Completed parsing of exclude files");
        } // END PARSING PERMANENT FILES

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] " << std::endl;

        auto home_node_policies = entries["home_node_policy"].error();
        if (!home_node_policies) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                      << "Warning: capio does not support home node policies yet! skipping section "
                      << std::endl;
        }

        if (entries["storage"].get_object().get(storage_section)) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                      << "No storage section found for workflow: " << workflow_name << std::endl;
            LOG("No storage section found for workflow: %s", std::string(workflow_name).c_str());
        } else {
            if (storage_section["memory"].get_array().get(storage_memory)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                          << "No files listed in memory storage section for workflow: "
                          << workflow_name << std::endl;
                LOG("No files listed in memory storage section for workflow: %s",
                    std::string(workflow_name).c_str());
            } else {
                for (auto file : storage_memory) {
                    std::string_view file_str;
                    file.get_string().get(file_str);
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                              << "Setting file " << file_str << " to be stored in memory"
                              << std::endl;
                    locations->setStoreFileInMemory(file_str);
                }
            }

            if (storage_section["fs"].get_array().get(storage_fs)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                          << "No files listed in fs storage section for workflow: " << workflow_name
                          << std::endl;
                LOG("No files listed in fs storage section for workflow: %s",
                    std::string(workflow_name).c_str());
            } else {
                for (auto file : storage_fs) {
                    std::string_view file_str;
                    file.get_string().get(file_str);
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                              << "Setting file " << file_str << " to be stored on file system"
                              << std::endl;
                    locations->setStoreFileInFileSystem(file_str);
                }
            }
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << " [ " << node_name << " ] "
                      << "Completed parsing of memory storage directives" << std::endl;
        }

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << std::endl;

        return locations;
    }
};

#endif // JSON_PARSER_HPP
