#include "StompClient.hpp"
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    
    std::string host = args.size() > 0 ? args[0] : "localhost";
    int port = args.size() > 1 ? std::stoi(args[1]) : 61613;
    
    try {
        StompClient::runTestClient(host, port);
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
