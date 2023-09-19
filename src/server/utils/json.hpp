#ifndef CAPIO_SERVER_UTILS_JSON_HPP
#define CAPIO_SERVER_UTILS_JSON_HPP

#include "simdjson/simdjson.h"

#include "utils/types.hpp"

using namespace simdjson;

void parse_conf_file(std::string conf_file, CSMetadataConfGlobs_t *metadata_conf_globs,
                     CSMetadataConfMap_t *metadata_conf, const std::string *capio_dir) {

    ondemand::parser parser;
    padded_string json;
    try {
        json = padded_string::load(conf_file);
    }
    catch (const simdjson_error &e) {
        std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "Exception thrown while opening config file: " << e.what() << std::endl;

        exit(1);
    }
    ondemand::document entries = parser.iterate(json);
    entries["name"];
    auto io_graph = entries["IO_Graph"];
    for (auto app: io_graph) {
        std::string_view app_name = app["name"].get_string();


        ondemand::array input_stream;
        auto error = app["input_stream"].get_array().get(input_stream);
        if (!error) {
            for (auto group: input_stream) {
                std::string_view group_name;
                error = group["group_name"].get_string().get(group_name);

#ifdef CAPIOLOG
                if (!error) {
                    auto files = group["files"];
                    for (auto file: files)
                        std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "file: " << file << std::endl;
                } else
                    std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "simple file" << group << std::endl;
#endif
            }
        }
        ondemand::array output_stream;
        error = app["output_stream"].get_array().get(output_stream);
        if (!error) {
            for (auto group: output_stream) {
                std::string_view group_name;
                error = group["group_name"].get_string().get(group_name);;
#ifdef CAPIOLOG
                if (!error) {
                    std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "group name " << group_name << std::endl;
                    auto files = group["files"];
                    for (auto file: files)
                        std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "file: " << file << std::endl;
                } else
                    std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "simple file" << group << std::endl;
#endif
            }
        }
        ondemand::array streaming;
        error = app["streaming"].get_array().get(streaming);
        if (!error) {
            for (auto file: streaming) {
                std::string_view name;
                error = file["name"].get_string().get(name);

                std::string_view committed;
                std::string committed_str;
                error = file["committed"].get_string().get(committed);
                std::string commit_rule = "";
                long int n_close = -1;
                if (!error) {


                    committed_str = std::string(committed);
                    int pos = committed_str.find(':');
                    if (pos != -1) {
                        commit_rule = committed_str.substr(0, pos);
                        if (commit_rule != "on_close") {
                            std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "error conf file: commit rule " << commit_rule << std::endl;
                            exit(1);
                        }
                        std::string n_close_str = committed_str.substr(pos + 1, committed_str.length());

                        if (!is_int(n_close_str)) {
                            std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "error conf file:  commit rule on_close invalid number" << std::endl;
                            exit(1);
                        }
                        n_close = std::stol(n_close_str);
                    } else
                        commit_rule = std::string(committed);
                } else {
                    std::cerr << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "error conf file: commit rule is mandatory in streaming section" << std::endl;
                    exit(1);
                }

                std::string_view mode;
                error = file["mode"].get_string().get(mode);

                long int n_files;
                error = file["n_files"].get_int64().get(n_files);
                if (error)
                    n_files = -1;


                long int batch_size;
                error = file["batch_size"].get_int64().get(batch_size);
                if (error)
                    batch_size = 0;
                std::string path = std::string(name);
                if (!is_absolute(&path)) {
                    if (path.substr(0, 2) == "./")
                        path = path.substr(2, path.length() - 2);
                    path = *capio_dir + "/" + path;
                }
                std::size_t pos = path.find('*');
                update_metadata_conf(path, pos, n_files, batch_size, std::string(commit_rule), std::string(mode),
                                     std::string(app_name),
                                     false, n_close, metadata_conf_globs, metadata_conf);
            }
        }
    }
    ondemand::array permanent_files;
    auto error = entries["permanent"].get_array().get(permanent_files);
    long int batch_size = 0;
    if (!error) {
        for (auto file: permanent_files) {
            std::string_view name;
            error = file.get_string().get(name);
            std::string path = std::string(name);

            if (!is_absolute(&path)) {
                if (path.substr(0, 2) == "./")
                    path = path.substr(2, path.length() - 2);
                path = *capio_dir + "/" + path;
            }
            if (!is_absolute(&path)) {
                if (path.substr(0, 2) == "./")
                    path = path.substr(2, path.length() - 2);
                path = *capio_dir + "/" + path;
            }
            std::size_t pos = path.find('*');
            if (pos == std::string::npos) {
                auto it = metadata_conf->find(path);
                if (it == metadata_conf->end())
                    update_metadata_conf(path, pos, -1, batch_size, "on_termination", "", "", true,
                                         -1, metadata_conf_globs, metadata_conf);
                else
                    std::get<4>(it->second) = true;
            } else {
                std::string prefix_str = path.substr(0, pos);
                long int i = match_globs(prefix_str, metadata_conf_globs);
                if (i == -1)
                    update_metadata_conf(path, pos, -1, batch_size, "on_termination", "", "", true,
                                         -1, metadata_conf_globs, metadata_conf);
                else {
                    auto &tuple = (*metadata_conf_globs)[i];
                    std::get<6>(tuple) = true;
                }
            }
        }
    }


}

#endif // CAPIO_SERVER_UTILS_JSON_HPP
