#ifndef CAPIO_SERVER_UTILS_JSON_HPP
#define CAPIO_SERVER_UTILS_JSON_HPP

#include <singleheader/simdjson.h>

#include "utils/location.hpp"
#include "utils/metadata.hpp"
#include "utils/types.hpp"

inline void load_configuration(const std::string &conf_file, const std::filesystem::path &capio_dir,
                               simdjson::padded_string &json) {
    CapioFileLocations file_locations;
    simdjson::ondemand::parser parser;

    std::unordered_map<std::string, std::vector<std::string_view>> alias_map;

    std::string_view workflow_name;
    START_LOG(gettid(), "call(config_file='%s', capio_dir='%s')", conf_file.c_str(),
              capio_dir.c_str());

    auto doc                           = parser.iterate(json);
    simdjson::ondemand::object objects = doc.get_object();

    try {
        workflow_name = objects["name"].get_string();
    } catch (simdjson::simdjson_error &e) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Current configuration file does not provide required section name"
                  << std::endl;
        ERR_EXIT("Current configuration file does not provide required section name");
    }
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Workflow name: " << workflow_name << std::endl;

    try {
        auto aliases = objects["alias"].get_object();
        for (auto alias : aliases) {
            std::string_view alias_name = alias.unescaped_key();
            std::vector<std::string_view> resolved_alias;
            for (auto t : alias.value().get_array().value()) {
                std::string_view tmp_str;
                t.get(tmp_str);
                resolved_alias.emplace_back(tmp_str);
            }
            alias_map.emplace(alias_name, resolved_alias);
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Alias: " << alias_name << " = [";
            for (auto str : resolved_alias) {
                std::cout << " " << str;
            }
            std::cout << " ]" << std::endl;
        }
    } catch (simdjson::simdjson_error &e) {
    }

    simdjson::simdjson_result<simdjson::ondemand::object> io_graph;

    try {
        io_graph = objects["io_graph"].get_object();
    } catch (simdjson::simdjson_error &e) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Current configuration file does not provide required section io_graph"
                  << std::endl;
        ERR_EXIT("Current configuration file does not provide required section io_graph");
    }

    for (auto application : io_graph) {
        auto application_name = std::string(application.unescaped_key().value());
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                  << "Parsing configuration for: " << application_name << std::endl;

        try {
            auto output = application.value()["output"].get_object();
            for (auto output_file : output) {
                std::string file_name = std::string(output_file.unescaped_key().value());
                file_locations.newFile(file_name);
                file_locations.addProducer(file_name, application_name);

                try {
                    auto commit_rule =
                        output_file.value()["committed"].value().get_string().value();
                    file_locations.setCommitRule(file_name, std::string(commit_rule));
                } catch (simdjson::simdjson_error &e) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "No committed rule for file " << file_name << std::endl;
                }
                try {
                    auto fire_rule = output_file.value()["mode"].value().get_string().value();
                    file_locations.setFireRule(file_name, std::string(fire_rule));
                } catch (simdjson::simdjson_error &e) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "No fire rule for file " << file_name << std::endl;
                }
            }
        } catch (simdjson::simdjson_error &e) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "No output files for app "
                      << application_name << std::endl;
        }

        try {
            auto input = application.value()["input"].get_object();
            for (auto input_file : input) {
                std::string file_name = std::string(input_file.unescaped_key().value());
                file_locations.newFile(file_name);
                file_locations.addConsumer(file_name, application_name);
            }
        } catch (simdjson::simdjson_error &e) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "No input files for app "
                      << application_name << std::endl;
        }
    }

    file_locations.print();
    exit(EXIT_SUCCESS);
}

void parse_conf_file(const std::string &conf_file, const std::filesystem::path &capio_dir) {
    START_LOG(gettid(), "call(config_file='%s', capio_dir='%s')", conf_file.c_str(),
              capio_dir.c_str());

    simdjson::ondemand::parser parser;
    simdjson::padded_string json;
    simdjson::ondemand::document entries;
    simdjson::ondemand::array input_stream, output_stream, streaming, permanent_files;
    simdjson::error_code error;

    try {
        json = simdjson::padded_string::load(conf_file);
    } catch (const simdjson::simdjson_error &e) {
        std::cerr << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                  << "Exception thrown while opening config file: " << e.what() << std::endl;
        LOG("Exception thrown while opening config file: %s", e.what());
        ERR_EXIT("Exception thrown while opening config file: %s", e.what());
    }

    entries = parser.iterate(json);

    try {
        auto version = entries["version"].get_int64();
        if (version.value() > 1) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Version of CLIO is " << version
                      << std::endl;
            load_configuration(conf_file, capio_dir, json);
            return;
        }
    } catch (simdjson::simdjson_error &e) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Version of CLIO is 1" << std::endl;
    }

    std::string_view wf_name;
    error = entries["name"].get_string().get(wf_name);
    if (error) {
        ERR_EXIT("Error: workflow name is mandatory");
    }
    workflow_name = std::string(wf_name);
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
              << "Parsing configuration for workflow: " << workflow_name << std::endl;
    LOG("Parsing configuration for workflow: %s", std::string(workflow_name).c_str());

    auto io_graph = entries["IO_Graph"];

    for (auto app : io_graph) {
        std::string_view app_name;
        error = app["name"].get_string().get(app_name);
        if (error) {
            ERR_EXIT("Error: app name is mandatory");
        }

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Parsing config for app " << app_name
                  << std::endl;
        LOG("Parsing config for app %s", std::string(app_name).c_str());

        if (app["input_stream"].get_array().get(input_stream)) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No input_stream section found for app " << app_name << std::endl;
            LOG("No input_stream section found for app %s", std::string(app_name).c_str());
        } else {
            // TODO: parse input_stream
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "Completed input_stream parsing for app: " << app_name << std::endl;
            LOG("Completed input_stream parsing for app: %s", std::string(app_name).c_str());
        }

        if (app["output_stream"].get_array().get(output_stream)) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No output_stream section found for app " << app_name << std::endl;
            LOG("No output_stream section found for app %s", std::string(app_name).c_str());
        } else {
            // TODO: parse output stream
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "Completed output_stream parsing for app: " << app_name << std::endl;
            LOG("Completed output_stream parsing for app: %s", std::string(app_name).c_str());
        }

        // PARSING STREAMING FILES
        if (app["streaming"].get_array().get(streaming)) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING
                      << "No streaming section found for app: " << app_name << std::endl;
            LOG("No streaming section found for app: %s", std::string(app_name).c_str());
        } else {
            LOG("Began parsing streaming section for app %s", std::string(app_name).c_str());
            for (auto file : streaming) {
                std::string_view name, committed, mode, commit_rule;
                long int n_close = -1;
                long n_files, batch_size;

                error = file["name"].get_string().get(name);
                if (error) {
                    ERR_EXIT("error name for application is mandatory");
                }
                LOG("Stream name: %s", std::string(name).c_str());

                // PARSING COMMITTED
                error = file["committed"].get_string().get(committed);
                if (error) {
                    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                              << "commit rule is mandatory in streaming section" << std::endl;
                    ERR_EXIT("error commit rule is mandatory in streaming section");
                } else {
                    auto pos = committed.find(':');
                    if (pos != std::string::npos) {
                        commit_rule = committed.substr(0, pos);
                        if (commit_rule != CAPIO_FILE_COMMITTED_ON_CLOSE) {
                            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "commit rule "
                                      << commit_rule << std::endl;
                            ERR_EXIT("error commit rule: %s", std::string(commit_rule).c_str());
                        }

                        std::string n_close_str(committed.substr(pos + 1, committed.length()));

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

                std::filesystem::path path(name);
                if (path.is_relative()) {
                    path = (capio_dir / path).lexically_normal();
                }
                LOG("path: %s", path.c_str());

                std::size_t pos = path.native().find('*');
                update_metadata_conf(path, pos, n_files, batch_size, std::string(commit_rule),
                                     std::string(mode), std::string(app_name), false, n_close);
            }

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                      << "completed parsing of streaming section for app: " << app_name
                      << std::endl;
            LOG("completed parsing of streaming section for app: %s",
                std::string(app_name).c_str());
        } // END PARSING STREAMING FILES

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
            error = file.get_string().get(name);
            if (error) {
                ERR_EXIT("error name for permanent section is mandatory");
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
}

#endif // CAPIO_SERVER_UTILS_JSON_HPP