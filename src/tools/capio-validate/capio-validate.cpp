#include <filesystem>
#include <iostream>
#include <string>

std::string workflow_name;

#include <args.hxx>
#include <capio/json.hpp>

int main(int argc, char **argv) {
    std::cout << CAPIO_LOG_SERVER_BANNER << std::endl;
    args::ArgumentParser parser(CAPIO_SERVER_ARG_PARSER_PRE, CAPIO_SERVER_ARG_PARSER_EPILOGUE);
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "help", "Display this help menu", {'h', "help"});

    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'f', "file"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << CAPIO_SERVER_ARG_PARSER_PRE_COMMAND << parser;
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        exit(EXIT_FAILURE);
    } catch (args::ValidationError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        exit(EXIT_FAILURE);
    }

    if (config) {
        std::string token                      = args::get(config);
        const std::filesystem::path &capio_dir = std::filesystem::current_path();
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "parsing config file: " << token
                  << std::endl;
        parse_conf_file(token, capio_dir);
    } else {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Error: no config file provided!"
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Completed JSON validation!" << std::endl
              << std::flush;
    return 0;
}