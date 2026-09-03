#include "cli/cli_driver.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return cactus::cli::run(argc, argv, std::cout, std::cerr);
}
