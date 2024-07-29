#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP
#include "cl_configuration.hpp"

#include <capio/env.hpp>

#include <singleheader/simdjson.h>

class JsonParser {

    inline static bool is_int(const std::string &s) {
        START_LOG(gettid(), "call(%s)", s.c_str());
        bool res = false;
        if (!s.empty()) {
            char *p;
            strtol(s.c_str(), &p, 10);
            res = *p == 0;
        }
        return res;
    }

  public:
    static CapioCLConfiguration *parse(const std::filesystem::path &source,
                                       const std::string &capio_dir) {
        auto locations = new CapioCLConfiguration();

        START_LOG(gettid(), "call(config_file='%s', capio_dir='%s')", source.c_str(),
                  capio_dir.c_str());

        locations->newFile(capio_dir);
        if (source.empty()) {
            delete locations;
            return nullptr;
        }

        simdjson::ondemand::parser parser;
        simdjson::padded_string json;
        simdjson::ondemand::document entries;
        simdjson::ondemand::array input_stream, output_stream, streaming, permanent_files;

        try {
            json = simdjson::padded_string::load(source.c_str());
        } catch (const simdjson::simdjson_error &e) {
            std::cerr << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                      << "Exception thrown while opening config file: " << e.what() << std::endl;
            LOG("Exception thrown while opening config file: %s", e.what());
            delete locations;
            return nullptr;
        }

        entries = parser.iterate(json);
        std::string_view wf_name;

        if (entries["name"].get_string().get(wf_name)) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Error: workflow name is mandatory"
                      << std::endl;
            delete locations;
            return nullptr;
        }
        workflow_name = std::string(wf_name);
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                  << "Parsing configuration for workflow: " << workflow_name << std::endl;
        LOG("Parsing configuration for workflow: %s", std::string(workflow_name).c_str());

        auto io_graph = entries["IO_Graph"];

        for (auto app : io_graph) {
            std::string_view app_name;

            if (app["name"].get_string().get(app_name)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Error: app name is mandatory"
                          << std::endl;
                delete locations;
                return nullptr;
            }

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Parsing config for app " << app_name
                      << std::endl;
            LOG("Parsing config for app %s", std::string(app_name).c_str());

            if (app["input_stream"].get_array().get(input_stream)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                          << "No input_stream section found for app " << app_name << std::endl;
                delete locations;
                return nullptr;
            }

            for (auto itm : input_stream) {
                std::filesystem::path file(itm.get_string().take_value());
                std::string appname(app_name);
                locations->newFile(file);
                locations->addConsumer(file, appname);
            }

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                      << "Completed input_stream parsing for app: " << app_name << std::endl;
            LOG("Completed input_stream parsing for app: %s", std::string(app_name).c_str());

            if (app["output_stream"].get_array().get(output_stream)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                          << "No output_stream section found for app " << app_name << std::endl;
                delete locations;
                return nullptr;
            }

            for (auto itm : output_stream) {
                std::filesystem::path file(itm.get_string().take_value());
                std::string appname(app_name);
                locations->newFile(file);
                locations->addConsumer(file, appname);
            }
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                      << "Completed output_stream parsing for app: " << app_name << std::endl;
            LOG("Completed output_stream parsing for app: %s", std::string(app_name).c_str());

            // PARSING STREAMING FILES
            if (app["streaming"].get_array().get(streaming)) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                          << "No streaming section found for app: " << app_name << std::endl;

            } else {
                LOG("Began parsing streaming section for app %s", std::string(app_name).c_str());
                for (auto file : streaming) {
                    std::string_view committed, mode, commit_rule;
                    std::vector<std::filesystem::path> streaming_names;
                    long int n_close = -1;
                    long n_files, batch_size;

                    simdjson::ondemand::array name;
                    bool is_file = true;

                    if (file["name"].get_array().get(name)) {
                        // name not found. test for directory
                        if (file["dirname"].get_array().get(name)) {
                            // both not found
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                      << "error: name for application is mandatory" << std::endl;
                            delete locations;
                            return nullptr;
                        }
                        // found directory
                        is_file = false;
                    }

                    if (name.is_empty()) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                  << "error: empty name/dirname section" << std::endl;
                        delete locations;
                        return nullptr;
                    }

                    for (auto item : name) {
                        std::string_view elem = item.get_string().value();
                        streaming_names.emplace_back(elem);
                        LOG("Found name: %s", std::string(elem).c_str());
                    }

                    // PARSING COMMITTED
                    if (file["committed"].get_string().get(committed)) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                  << "commit rule is mandatory in streaming section" << std::endl;
                        delete locations;
                        return nullptr;
                    }
                    auto pos = committed.find(':');
                    if (pos != std::string::npos) {
                        commit_rule = committed.substr(0, pos);

                        if (commit_rule == CAPIO_FILE_COMMITTED_ON_CLOSE) {

                            std::string n_close_str(committed.substr(pos + 1, committed.length()));

                            if (!is_int(n_close_str)) {
                                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                          << "commit rule on_close invalid number" << std::endl;
                                delete locations;
                                return nullptr;
                            }
                            n_close = std::stol(n_close_str);
                        } else if (commit_rule == CAPIO_FILE_COMMITTED_ON_FILE) {
                            // TODO: support CoF rule
                        } else if (commit_rule == CAPIO_FILE_COMMITTED_AT_N_FILES) {
                            // TODO: support n_files
                        } else {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                      << "Error: ':' found on commit rule not supporting operator!"
                                      << std::endl;
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                      << "invalid token is: " << commit_rule << std::endl;
                            delete locations;
                            return nullptr;
                        }

                    } else {
                        commit_rule = committed;
                    }

                    LOG("Committed: %s", std::string(committed).c_str());
                    // END PARSING COMMITTED

                    if (file["mode"].get_string().get(mode)) {
                        mode = CAPIO_FILE_MODE_UPDATE;
                    }
                    LOG("Mode: %s", std::string(mode).c_str());

                    if (file["n_files"].get_int64().get(n_files)) {
                        n_files = -1;
                    }
                    LOG("n_files: %d", n_files);

                    if (file["batch_size"].get_int64().get(batch_size)) {
                        batch_size = 0;
                    }
                    LOG("batch_size: %d", batch_size);
                    for (auto path : streaming_names) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                                  << "Updating metadata for path:  " << path << std::endl;
                        if (path.is_relative()) {
                            path = (capio_dir / path).lexically_normal();
                        }
                        LOG("path: %s", path.c_str());

                        std::size_t pos = path.native().find('*');

                        // TODO: check for globs
                        std::string commit(commit_rule), firerule(mode);
                        if (n_files != -1) {
                            locations->setDirectory(path);
                            locations->setDirectoryFileCount(path, n_files);
                        }
                        locations->setCommitRule(path, commit);
                        locations->setFireRule(path, firerule);
                        locations->setCommitedNumber(path, n_close);
                        if (!is_file) {
                            locations->setDirectory(path);
                        } else {
                            locations->setFile(path);
                        }
                    }
                }

                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                          << "completed parsing of streaming section for app: " << app_name
                          << std::endl;
                LOG("completed parsing of streaming section for app: %s",
                    std::string(app_name).c_str());
            }

        } // END OF APP MAIN LOOPS
        LOG("Completed parsing of io_graph app main loops");

        long int batch_size = 0;
        if (entries["permanent"].get_array().get(permanent_files)) { // PARSING PERMANENT FILES
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No permanent section found for workflow: " << workflow_name << std::endl;
            LOG("No permanent section found for workflow: %s", std::string(workflow_name).c_str());
        } else {
            for (auto file : permanent_files) {
                std::string_view name;
                if (file.get_string().get(name)) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                              << "error name for permanent section is mandatory" << std::endl;
                    delete locations;
                    return nullptr;
                }
                LOG("Permanent name: %s", std::string(name).c_str());

                std::filesystem::path path(name);

                if (path.is_relative()) {
                    path = (capio_dir / path).lexically_normal();
                }
                // NOTE: here there was a copy of the previous structured block.
                // pretty much sure it is a bug, but it might be wanted...

                std::size_t pos = path.native().find('*');
                // TODO: check for globs

                locations->setPermanent(name.data(), true);
            }
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Completed parsing of permanent files"
                      << std::endl;
            LOG("Completed parsing of permanent files");
        } // END PARSING PERMANENT FILES

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Completed parsing of io_graph"
                  << std::endl;
        LOG("Completed parsing of io_graph");

        auto home_node_policies = entries["home_node_policy"].error();
        if (!home_node_policies) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "Warning: capio does not support home node policies yet! skipping section "
                      << std::endl;
        }

        return locations;
    }
};

#endif // JSON_PARSER_HPP
