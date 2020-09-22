#include "../../config_reader/config_reader.hpp"

int main(int argc, char** argv) {
    config_type config = get_deployment_config("../../../deployment.yaml");
    std::cout << "print config" << std::endl;
    for (auto const& pair1: config) {
        std::cout << "{" << pair1.first << std::endl;
        for (auto const& pair2: pair1.second) {
            std::cout << "{" << pair2.first << std::endl;
            for (auto const& rank: pair2.second) {
                std::cout << rank << std::endl;
            }
            std::cout << "}" << std::endl;
        }
        std::cout << "}" << std::endl;
    }
}