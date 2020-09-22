#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

typedef std::unordered_map<std::string, std::unordered_map<std::string, std::vector<int>>> config_type;

config_type get_deployment_config(std::string path) {
    config_type config;
    std::string line;
    std::ifstream myfile(path);
    std::string node = "";
    std::string app = "";
    if (myfile.is_open()) {
        while ( getline (myfile,line) ) {
            if (line.substr(0, 2) == "  ") {
                if (line.substr(0, 4) == "    ") {
                    config[node][app].push_back(std::stoi(line.substr(6, line.length() - 6)));
                }
                else {
                    app = line;
                    config[node][app] = std::vector<int>();
                }
            }
            else {
                node = line;
            }
        }
        myfile.close();
    }
    else
        std::cout << "Unable to open file";
    return config;
}

