#include <algorithm>
#include <args.hxx>
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <semaphore.h>
#include <singleheader/simdjson.h>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "args.hxx"

#include "capio/queue.hpp"

int main(int argc, char *argv[]) {

    auto workflow_name = get_capio_workflow_name();

    // check canary of capio server

    if (shm_open(get_capio_workflow_name().c_str(), O_RDONLY, 0) == -1) {
        std::cout << "No capio instances found for workflow " << get_capio_workflow_name()
                  << "! Aborting" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto tx = new CircularBuffer<char>("RX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name,
                                       false);

    auto rx = new CircularBuffer<char>("TX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name,
                                       false);

    args::ArgumentParser parser("Parser for capioctl");
    args::Group commands(parser, "commands");
    args::Command get(commands, "get", "Retrieve information's on object");
    args::Command set(commands, "set", "Configure the CAPIO server");

    args::ValueFlag<std::string> apps(
        get, "apps", "[ all , ... ]List the currently registered apps with CAPIO", {"apps"});
    args::ValueFlag<std::string> config(
        get, "config", "[ all, ... ] Retrieve the currently loaded CAPIO-CL configuration",
        {"config"});

    args::HelpFlag h(commands, "help", "help", {'h', "help"});
    args::HelpFlag h_get(get, "help", "help", {'h', "help"});

    try {
        parser.ParseCLI(argc, argv);

        if (get) {
            if (apps) {
                auto option = apps.Get();

                if (option == "all") {
                    std::cout << "Fetching all applications registered with server" << std::endl;
                } else {
                    // Fetch for given app
                    std::cout << "Fetching application registered with server with name " << option
                              << std::endl;
                }
            }

            if (config) {
            }
        }

    } catch (args::Help) {
        std::cout << parser;
    } catch (args::Error &e) {
        std::cerr << e.what() << std::endl << parser;
        return 1;
    }

    tx->write("CIAO PIPPO");

    char result[256];

    rx->read(result);

    std::cout << result << std::endl;

    delete tx;
    delete rx;

    return 0;
}