#include "StompTestBench.hpp"
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    
    try {
        StompLoadTester::main(args);
    } catch (const std::exception& e) {
        std::cerr << "Load tester error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
