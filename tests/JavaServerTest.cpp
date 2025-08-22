#include "StompClient.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<int> received_count{0};
std::atomic<bool> connection_success{false};

void onMessage(const std::string& dest, const std::string& body, 
              const std::map<std::string, std::string>& headers) {
    std::cout << "📨 [C++] Received: " << body << " on " << dest << std::endl;
    received_count++;
}

int main() {
    try {
        std::cout << "🔌 [C++] Creating client with credentials..." << std::endl;
        StompClient client("localhost", 61613, "cpp_client", "test123");
        
        std::cout << "🔌 [C++] Connecting to Java server..." << std::endl;
        auto future = client.connect();
        
        // Wait for connection with timeout
        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            std::cout << "✅ [C++] Connected to Java server!" << std::endl;
            connection_success = true;
            
            // Give connection time to fully establish
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Subscribe to test queue
            std::cout << "📡 [C++] Subscribing to /queue/cross-test..." << std::endl;
            std::string subId = client.subscribe("/queue/cross-test", onMessage);
            std::cout << "✅ [C++] Subscribed with ID: " << subId << std::endl;
            
            // Send test messages
            std::cout << "📤 [C++] Sending messages to Java server..." << std::endl;
            client.send("/queue/cross-test", "Hello Java from C++!");
            client.send("/queue/cross-test", "Cross-language test message 1");
            client.send("/queue/cross-test", "Cross-language test message 2");
            
            // Wait for any responses
            std::cout << "⏳ [C++] Waiting for message processing..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            // Test different destinations
            client.send("/topic/broadcast", "C++ broadcast message");
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            std::cout << "🔌 [C++] Disconnecting..." << std::endl;
            client.disconnect();
            
            std::cout << "📊 [C++] Messages received: " << received_count.load() << std::endl;
            std::cout << "🎉 [C++] Test completed successfully!" << std::endl;
            
        } else {
            std::cout << "❌ [C++] Connection timed out" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ [C++] Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
