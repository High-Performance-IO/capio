#ifndef CAPIO_SERVER_UTILS_JSON_HPP
#define CAPIO_SERVER_UTILS_JSON_HPP

#include <singleheader/simdjson.h>

#include "metadata.hpp"
#include "types.hpp"

inline bool is_int(const std::string &s) {
    START_LOG(gettid(), "call(%s)", s.c_str());
    bool res = false;
    if (!s.empty()) {
        char *p;
        strtol(s.c_str(), &p, 10);
        res = *p == 0;
    }
    return res;
}

void parse_conf_file(const std::string &conf_file, const std::filesystem::path &capio_dir) {
    START_LOG(gettid(), "call(config_file='%s', capio_dir='%s')", conf_file.c_str(),
              capio_dir.c_str());

    simdjson::ondemand::parser parser;
    simdjson::padded_string json;
    simdjson::ondemand::document entries;
    simdjson::ondemand::array output_stream, streaming, permanent_files;
    simdjson::error_code error;

    try {
        json = simdjson::padded_string::load(conf_file);
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Exception thrown while opening config file: " << e.what() << std::endl;
        LOG("Exception thrown while opening config file: %s", e.what());
        ERR_EXIT("Exception thrown while opening config file: %s", e.what());
    }

    entries      = parser.iterate(json);
    auto wf_name = entries["name"];

    if (wf_name.error()) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Error:" << error_message(wf_name.error())
                  << std::endl;
        ERR_EXIT("Error: workflow name is mandatory");
    }
    workflow_name = std::string(wf_name.get_string().value());
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
              << "Parsing configuration for workflow: " << workflow_name << std::endl;
    LOG("Parsing configuration for workflow: %s", std::string(workflow_name).c_str());

    auto io_graph = entries["IO_Graph"];
    if (io_graph.error()) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Error: missing \033[0;31m IO_STREAM \033[0m section!" << std::endl;
    }
    for (auto app : io_graph) {
        auto app_name = app["name"];

        if (app_name.error()) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Error:" << error_message(error)
                      << std::endl;
            ERR_EXIT("Error: app name is mandatory");
        }

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Parsing config for app " << app_name
                  << std::endl;
        LOG("Parsing config for app %s", std::string(app_name.get_string().value()).c_str());

        auto input_stream = app["input_stream"];
        if (input_stream.error()) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No input_stream section found for app " << app_name << std::endl;
            LOG("No input_stream section found for app %s",
                std::string(app_name.get_string().value()).c_str());
        } else {
            // TODO: parse input_stream
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "Completed input_stream parsing for app: " << app_name << std::endl;
            LOG("Completed input_stream parsing for app: %s",
                std::string(app_name.get_string().value()).c_str());
        }

        auto output_stream = app["output_stream"];
        if (output_stream.error()) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No output_stream section found for app " << app_name << std::endl;
            LOG("No output_stream section found for app %s",
                std::string(app_name.get_string().value()).c_str());
        } else {
            // TODO: parse output stream
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "Completed output_stream parsing for app: " << app_name << std::endl;
            LOG("Completed output_stream parsing for app: %s",
                std::string(app_name.get_string().value()).c_str());
        }

        // PARSING STREAMING FILES
        auto streaming = app["streaming"];
        if (streaming.error()) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No streaming section found for app: " << app_name << std::endl;
            LOG("No streaming section found for app: %s",
                std::string(app_name.get_string().value()).c_str());
        } else {
            LOG("Began parsing streaming section for app %s",
                std::string(app_name.get_string().value()).c_str());
            for (auto file : streaming) {
                std::string_view commit_rule;
                std::vector<std::filesystem::path> streaming_names;
                long int n_close = -1;
                long n_files, batch_size;

                // gathering name section
                simdjson::ondemand::array name = file["name"].get_array();
                if (name.is_empty()) {
                    name = file["dirname"].get_array();
                    if (name.is_empty()) {
                        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                  << "error: name / dirname for application is mandatory"
                                  << std::endl;
                        ERR_EXIT("error: name for application is mandatory");
                    }
                }
                for (auto item : name) {
                    std::string_view elem = item.get_string().value();
                    streaming_names.emplace_back(elem);
                    LOG("Found name: %s", std::string(elem).c_str());
                }

                // PARSING COMMITTED
                auto committed = file["committed"];
                if (committed.error()) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                              << "commit rule is mandatory in streaming section" << std::endl;
                    ERR_EXIT("error commit rule is mandatory in streaming section");
                } else {
                    auto committed_string = committed.get_string().value();
                    auto pos              = committed_string.find(':');
                    if (pos != std::string::npos) {
                        commit_rule = committed_string.substr(0, pos);
                        if (commit_rule != CAPIO_FILE_COMMITTED_ON_CLOSE) {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "commit rule "
                                      << commit_rule << std::endl;
                            ERR_EXIT("error commit rule: %s", std::string(commit_rule).c_str());
                        }

                        std::string n_close_str(
                            committed_string.substr(pos + 1, committed_string.length()));

                        if (!is_int(n_close_str)) {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                                      << "commit rule on_close invalid number" << std::endl;
                            ERR_EXIT("error commit rule on_close invalid number: !is_int()");
                        }
                        n_close = std::stol(n_close_str);
                    } else {
                        commit_rule = committed;
                    }
                }
                LOG("Committed: %s", std::string(committed.get_string().value()).c_str());

                // END PARSING COMMITTED

                auto mode = file["mode"];
                std::string mode_str;
                if (mode.error()) {
                    mode_str = CAPIO_FILE_MODE_UPDATE;
                } else {
                    mode_str = mode.get_string().value();
                }
                LOG("Mode: %s", std::string(mode.get_string().value()).c_str());

                auto n_files_simdjson = file["n_files"];
                if (n_files_simdjson.error()) {
                    n_files = -1;
                } else {
                    n_files = n_files_simdjson.get_int64().value();
                }
                LOG("n_files: %d", n_files);

                auto batch_size_json = file["batch_size"];
                if (batch_size_json.error()) {
                    batch_size = 0;
                } else {
                    batch_size = batch_size_json.get_int64().value();
                }
                LOG("batch_size: %d", batch_size);

                for (auto path : streaming_names) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                              << "Updating metadata for path:  " << path << std::endl;
                    if (path.is_relative()) {
                        path = (capio_dir / path).lexically_normal();
                    }
                    LOG("path: %s", path.c_str());

                    std::size_t pos = path.native().find('*');
                    update_metadata_conf(path, pos, n_files, batch_size, std::string(commit_rule),
                                         std::string(mode.get_string().value()),
                                         std::string(app_name.get_string().value()), false,
                                         n_close);
                }
            }

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "completed parsing of streaming section for app: " << app_name
                      << std::endl;
            LOG("completed parsing of streaming section for app: %s",
                std::string(app_name.get_string().value()).c_str());
        } // END PARSING STREAMING FILES

    } // END OF APP MAIN LOOPS
    LOG("Completed parsing of io_graph app main loops");

    long int batch_size = 0;

    auto permanent_files_json = entries["permanent"];
    if (permanent_files_json.error()) { // PARSING PERMANENT FILES
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "No permanent section found for workflow: " << workflow_name << std::endl;
        LOG("No permanent section found for workflow: %s", std::string(workflow_name).c_str());
    } else {
        for (auto file : permanent_files_json) {
            std::string name;
            auto name_json = file.get_string();
            if (name_json.error()) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                          << "Error: name for permanent section mandatory" << std::endl;
                ERR_EXIT("error name for permanent section is mandatory");
            } else {
                name = name_json.value();
            }
            LOG("Permanent name: %s", std::string(name).c_str());

            std::filesystem::path path(name);

            if (path.is_relative()) {
                path = (capio_dir / path).lexically_normal();
            }
            // NOTE: here there was a copy of the previous structured block.
            // pretty much sure it is a bug, but it might be wanted...

            std::size_t pos = path.native().find('*');
            if (pos == std::string::npos) {
                auto it = metadata_conf.find(path);
                if (it == metadata_conf.end()) {
                    update_metadata_conf(path, pos, -1, batch_size,
                                         CAPIO_FILE_COMMITTED_ON_TERMINATION,
                                         CAPIO_FILE_MODE_UPDATE, "", true, -1);
                } else {
                    std::get<4>(it->second) = true;
                }
            } else {
                std::string prefix_str = path.native().substr(0, pos);
                long int i             = match_globs(prefix_str);
                if (i == -1) {
                    update_metadata_conf(path, pos, -1, batch_size,
                                         CAPIO_FILE_COMMITTED_ON_TERMINATION, "", "", true, -1);
                } else {
                    auto &tuple        = metadata_conf_globs[i];
                    std::get<6>(tuple) = true;
                }
            }
        }
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Completed parsing of permanent files"
                  << std::endl;
        LOG("Completed parsing of permanent files");
    } // END PARSING PERMANENT FILES

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Completed parsing of io_graph" << std::endl;
    LOG("Completed parsing of io_graph");

    auto home_node_policies = entries["home_node_policy"];
    if (home_node_policies.error()) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "No home node policy found" << std::endl;
    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                  << "Warning: capio does not support home node policies yet!" << std::endl;
    }
}

#endif // CAPIO_SERVER_UTILS_JSON_HPP
