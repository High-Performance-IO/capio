#ifndef CAPIO_SERVER_UTILS_JSON_HPP
#define CAPIO_SERVER_UTILS_JSON_HPP

#include <singleheader/simdjson.h>

#include "utils/metadata.hpp"
#include "utils/types.hpp"

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
        std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR
                  << "Exception thrown while opening config file: " << e.what() << std::endl;
        ERR_EXIT("Exception thrown while opening config file: %s", e.what());
    }

    entries                              = parser.iterate(json);
    const std::string_view workflow_name = entries["name"].get_string();

    std::cout << CAPIO_SERVER_CLI_LOG_SERVER
              << "Parsing configuration for workflow: " << workflow_name << std::endl;

    auto io_graph = entries["IO_Graph"];

    for (auto app : io_graph) {
        std::string_view app_name = app["name"].get_string();

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Parsing config for app " << app_name
                  << std::endl;

        if (app["input_stream"].get_array().get(input_stream)) {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING
                      << "No input_stream section found for app " << app_name << std::endl;
        } else {
            // TODO: parse input_stream
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                      << "Completed input_stream parsing for app: " << app_name << std::endl;
        }

        if (app["output_stream"].get_array().get(output_stream)) {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING
                      << "No output_stream section found for app " << app_name << std::endl;
        } else {
            // TODO: parse output stream
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                      << "Completed output_stream parsing for app: " << app_name << std::endl;
        }

        // PARSING STREAMING FILES
        if (app["streaming"].get_array().get(streaming)) {
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING
                      << "No streaming section found for app: " << app_name << std::endl;
        } else {
            for (auto file : streaming) {
                std::string_view name, committed;
                std::string committed_str, commit_rule;
                long int n_close = -1;

                error = file["name"].get_string().get(name);

                // PARSING COMMITTED
                if (!file["committed"].get_string().get(committed)) {

                    committed_str = std::string(committed);
                    int pos       = committed_str.find(':');
                    if (pos != -1) {
                        commit_rule = committed_str.substr(0, pos);
                        if (commit_rule != CAPIO_FILE_MODE_ON_CLOSE) {
                            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "commit rule "
                                      << commit_rule << std::endl;
                            ERR_EXIT("error conf file");
                        }

                        std::string n_close_str =
                            committed_str.substr(pos + 1, committed_str.length());

                        if (!is_int(n_close_str)) {
                            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR
                                      << "commit rule on_close invalid number" << std::endl;
                            ERR_EXIT("error conf file");
                        }
                        n_close = std::stol(n_close_str);
                    } else {
                        commit_rule = std::string(committed);
                    }
                } else {
                    std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR
                              << "commit rule is mandatory in streaming section" << std::endl;
                    ERR_EXIT("error conf file");
                }
                // END PARSING COMMITTED

                std::string_view mode;
                error = file["mode"].get_string().get(mode);

                long n_files;
                if (file["n_files"].get_int64().get(n_files)) {
                    n_files = -1;
                }
                long batch_size;
                error = file["batch_size"].get_int64().get(batch_size);
                if (error) {
                    batch_size = 0;
                }
                std::filesystem::path path(name);
                if (path.is_relative()) {
                    path = (capio_dir / path).lexically_normal();
                }
                std::size_t pos = path.native().find('*');
                update_metadata_conf(path, pos, n_files, batch_size, std::string(commit_rule),
                                     std::string(mode), std::string(app_name), false, n_close);
            }

            std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                      << "completed parsing of streaming section for app: " << app_name
                      << std::endl;
        } // END PARSING STREAMING FILES

    } // END OF APP MAIN LOOPS

    long int batch_size = 0;
    if (entries["permanent"].get_array().get(permanent_files)) { // PARSING PERMANENT FILES
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING
                  << "No permanent section found for workflow: " << workflow_name << std::endl;
    } else {
        for (auto file : permanent_files) {
            std::string_view name;
            error = file.get_string().get(name);
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
                    update_metadata_conf(path, pos, -1, batch_size, CAPIO_FILE_MODE_ON_TERMINATION,
                                         "", "", true, -1);
                } else {
                    std::get<4>(it->second) = true;
                }
            } else {
                std::string prefix_str = path.native().substr(0, pos);
                long int i             = match_globs(prefix_str);
                if (i == -1) {
                    update_metadata_conf(path, pos, -1, batch_size, CAPIO_FILE_MODE_ON_TERMINATION,
                                         "", "", true, -1);
                } else {
                    auto &tuple        = metadata_conf_globs[i];
                    std::get<6>(tuple) = true;
                }
            }
        }
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Completed parsing of permanent files"
                  << std::endl;
    } // END PARSING PERMANENT FILES

    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Completed parsing of io_graph" << std::endl;
}

#endif // CAPIO_SERVER_UTILS_JSON_HPP
